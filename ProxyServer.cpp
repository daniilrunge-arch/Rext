#include "ProxyServer.hpp"
#include "Shutdown.hpp"
#include "ConsoleInput.hpp"
#include "Logger.hpp"
#include "NativeRtt.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#ifndef _WIN32
#include <fcntl.h>
#endif

#ifdef _WIN32
  #pragma comment(lib, "ws2_32.lib")
  static bool winsock_inited = false;
  static void init_winsock() {
      if (!winsock_inited) {
          WSADATA wsa;
          WSAStartup(MAKEWORD(2,2), &wsa);
          winsock_inited = true;
      }
  }
#else
  static void init_winsock() {}
#endif

// ---------- helpers ----------
static std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::int64_t steadyMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static socket_t connectTarget(const RoutingServer& target, int seconds) {
    addrinfo hints{}, *res=nullptr; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(target.host.c_str(),std::to_string(target.port).c_str(),&hints,&res)!=0 || !res) return INVALID_SOCK;
    socket_t sock=::socket(res->ai_family,res->ai_socktype,res->ai_protocol);
    if(sock==INVALID_SOCK) {freeaddrinfo(res); return INVALID_SOCK;}
#ifdef _WIN32
    u_long mode=1; ioctlsocket(sock,FIONBIO,&mode);
#else
    int flags=fcntl(sock,F_GETFL,0); fcntl(sock,F_SETFL,flags|O_NONBLOCK);
#endif
    bool ok=connect(sock,res->ai_addr,static_cast<int>(res->ai_addrlen))==0;
    freeaddrinfo(res);
    if(!ok) {
        fd_set writes, errors; FD_ZERO(&writes); FD_SET(sock,&writes); FD_ZERO(&errors); FD_SET(sock,&errors);
        timeval tv{(std::max)(1,seconds),0};
        if(select(static_cast<int>(sock+1),nullptr,&writes,&errors,&tv)>0) {
            int error=0;
#ifdef _WIN32
            int len=sizeof(error);
#else
            socklen_t len=sizeof(error);
#endif
            ok=getsockopt(sock,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&len)==0 && error==0;
        }
    }
#ifdef _WIN32
    mode=0; ioctlsocket(sock,FIONBIO,&mode); if(!ok) {closesocket(sock);return INVALID_SOCK;}
#else
    fcntl(sock,F_SETFL,flags); if(!ok) {::close(sock);return INVALID_SOCK;}
#endif
    return sock;
}

ProxyServer::ProxyServer(Config cfg) : cfg_(std::move(cfg)) {
    init_winsock();
    warps_ = std::make_unique<WarpManager>(this);
    // load warps from config if present
    if (!cfg_.warps.empty()) {
        warps_->setWarps(cfg_.warps);
    }
    warps_->loadSaved();
    rest_ = std::make_unique<RestApi>(cfg_, sessionsMtx_, sessions_);
    ext_  = std::make_unique<ExtensionManager>(this);
}

ProxyServer::~ProxyServer() {
    stop();
    consoleAlive_->store(false);
    if(consoleThread_.joinable())consoleThread_.join();
    if (rest_) rest_->stop();
    closeSock(listenSock_);
    for(auto& sock:extraListeners_)closeSock(sock);
    for(auto& thread:listenerThreads_)if(thread.joinable())thread.join();
    while(workers_.load()!=0)std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void ProxyServer::stop() {
    std::lock_guard<std::mutex> guard(stopMutex_);
    if(!running_.exchange(false))return;
    consoleAlive_->store(false);
    std::cout<<"[SHUTDOWN] Server restarting; notifying players.\n";
    std::vector<std::shared_ptr<Session>> copy;
    {std::lock_guard<std::mutex> lock(sessionsMtx_);for(auto& e:sessions_)copy.push_back(e.second);}
    for(auto& session:copy)disconnectSession(session,cfg_.phrase("close"));
    if(rest_)rest_->stop();
    closeSock(listenSock_);for(auto& socket:extraListeners_)closeSock(socket);
}
void ProxyServer::sendRealIp(const std::shared_ptr<Session>& s,socket_t socket,std::uint64_t generation){
    auto target=findServer(s->currentServer);
    if(!target || target->isVanilla)return;
    std::vector<uint8_t> body{67,0,0};auto ip=encodeCsharpString(s->ip);
    body.insert(body.end(),ip.begin(),ip.end());
    if(!sendToBackend(s,socket,generation,framePacket(body)))
        std::cout<<"[WARN] [REALIP] Delivery failed for session "<<s->id<<"\n";
}

void ProxyServer::closeSock(socket_t& s) {
    if (s != INVALID_SOCK) {
        // shutdown() wakes threads blocked in recv()/accept() before the handle is closed.
#ifdef _WIN32
        shutdown(s, SD_BOTH);
        closesocket(s);
#else
        shutdown(s, SHUT_RDWR);
        ::close(s);
#endif
        s = INVALID_SOCK;
    }
}

void ProxyServer::setNodelay(socket_t s) {
    if(!cfg_.socketNoDelay)return;
    int one = 1;
#ifdef _WIN32
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
#else
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
}

bool ProxyServer::sendAll(socket_t sock, const uint8_t* data, size_t n) {
    if (sock == INVALID_SOCK || n == 0) return false;
    size_t sent = 0;
    while (sent < n) {
#ifdef _WIN32
        int r = ::send(sock, (const char*)data + sent, (int)(n - sent), 0);
#else
        ssize_t r = ::send(sock, data + sent, n - sent, MSG_NOSIGNAL);
#endif
        if (r <= 0) return false;
        sent += size_t(r);
    }
    return true;
}

bool ProxyServer::sendToClient(const std::shared_ptr<Session>& s,
                               const std::vector<uint8_t>& data,
                               std::uint64_t expectedGeneration, bool allowClosed) {
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->clientWriteMtx);
    if ((!allowClosed && s->closed) || s->client == INVALID_SOCK) return false;
    if (expectedGeneration != 0 && s->backendGeneration.load() != expectedGeneration) return false;
    size_t offset=0;
    while(offset+3<=data.size()) {
        size_t length=data[offset]|(size_t(data[offset+1])<<8);
        if(length<3 || offset+length>data.size()) break;
        s->entities.observe(std::vector<uint8_t>(data.begin()+offset,data.begin()+offset+length));
        offset+=length;
    }
    return sendAll(s->client, data);
}

bool ProxyServer::sendToBackend(const std::shared_ptr<Session>& s,
                                const std::vector<uint8_t>& data) {
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->backendWriteMtx);
    if (!s->backendReady || s->backend == INVALID_SOCK) return false;
    return sendAll(s->backend, data);
}

bool ProxyServer::sendToBackend(const std::shared_ptr<Session>& s, socket_t expectedSocket,
                                std::uint64_t expectedGeneration,
                                const std::vector<uint8_t>& data) {
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->backendWriteMtx);
    if (s->backendGeneration.load() != expectedGeneration ||
        s->backend != expectedSocket || !s->backendReady || expectedSocket == INVALID_SOCK)
        return false;
    return sendAll(expectedSocket, data);
}

const RoutingServer* ProxyServer::findServer(const std::string& name) const {
    std::string n = toLower(name);
    for (auto& s : cfg_.servers)
        if (toLower(s.name) == n) return &s;
    return nullptr;
}

const RoutingServer* ProxyServer::defaultServer() const {
    return cfg_.servers.empty() ? nullptr : &cfg_.servers[0];
}

const RoutingServer* ProxyServer::lobbyServer() const {
    for (auto& s : cfg_.servers)
        if (toLower(s.name) == "lobby") return &s;
    return defaultServer();
}

void ProxyServer::sendChat(std::shared_ptr<Session> s, const std::string& text,
                           uint8_t r, uint8_t g, uint8_t b) {
    if (!s || s->closed) return;
    auto pkt = buildChatMessage(text, r, g, b);
    sendToClient(s, pkt);
}

void ProxyServer::disconnectSession(std::shared_ptr<Session> s, const std::string& reason) {
    if (!s || s->closed) return;
    s->closed = true;
    auto pkt = buildDisconnect(reason);
    if (sendToClient(s, pkt, 0, true))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> lock(s->clientWriteMtx);
        closeSock(s->client);
    }
    {
        std::lock_guard<std::mutex> lock(s->backendWriteMtx);
        closeSock(s->backend);
    }
    s->backendReady = false;
}

// ---------- start / accept ----------
void ProxyServer::start() {
    ext_->loadExtensions();
    if(cfg_.restEnabled)rest_->start();

    listenSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int one = 1;
    setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(uint16_t(cfg_.listenPort));

    if (bind(listenSock_, (sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(listenSock_, 128) < 0) {
        std::cerr << "[FATAL] bind/listen failed on " << cfg_.listenPort << "\n";
        return;
    }

    std::cout << "[ROCKET] REXT-PROXY is ONLINE on port " << cfg_.listenPort << "\n";
    std::cout << "[ROCKET] Version string: " << cfg_.terrariaVersion << "\n";
    std::cout << "[CORE] listen=0.0.0.0:" << cfg_.listenPort
              << " capacity=" << cfg_.maxPlayers << " protocol=326\n";
    std::cout << "[ROUTING] Backend servers:\n";
    for (const auto& server : cfg_.servers)
        std::cout << "  * " << server.name << " -> " << server.host << ':' << server.port << "\n";
    std::cout << "[CONSOLE] \x1b[93mhud, list, dim, move, kick, ban, banip, bans, warps, modules, plugins, help, quit\x1b[0m\n";
    std::cout << "[MANAGER] Temporary rules database: " << playerRules_.path() << "\n";

    for(const auto& l:cfg_.listeners) {
        if(l.port==cfg_.listenPort)continue;
        socket_t sock=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        sockaddr_in other{};other.sin_family=AF_INET;other.sin_addr.s_addr=htonl(INADDR_ANY);other.sin_port=htons(uint16_t(l.port));
        setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<char*>(&one),sizeof(one));
        if(bind(sock,reinterpret_cast<sockaddr*>(&other),sizeof(other))<0||listen(sock,128)<0) {
            closeSock(sock);running_=false;closeSock(listenSock_);for(auto& x:extraListeners_)closeSock(x);
            throw std::runtime_error("Cannot bind configured listenPort "+std::to_string(l.port));
        }
        extraListeners_.push_back(sock);
        listenerThreads_.emplace_back([this,sock,l]{acceptLoop(sock,l.port);});
        std::cout<<"[LISTEN] 0.0.0.0:"<<l.port<<" pool="<<l.pool.size()<<"\n";
    }
    consoleThread_=std::thread([this]{ consoleLoop(); });
    std::cout << "[HUD] Введите hud для панели, plugins для расширений.\n";
    acceptLoop(listenSock_,cfg_.listenPort);
    for(auto& thread:listenerThreads_)if(thread.joinable())thread.join();
}

void ProxyServer::acceptLoop(socket_t listener,int port) {
    while (running_) {
        if(proxyStopRequested){stop();break;}
        fd_set ready;FD_ZERO(&ready);FD_SET(listener,&ready);timeval poll{0,200000};
        if(select(static_cast<int>(listener+1),&ready,nullptr,nullptr,&poll)<=0)continue;
        if(!running_)break;
        sockaddr_in peer{};
#ifdef _WIN32
        int plen = static_cast<int>(sizeof(peer));
#else
        socklen_t plen = sizeof(peer);
#endif
        socket_t cs = accept(listener, (sockaddr*)&peer, &plen);
        if (cs == INVALID_SOCK) continue;

        setNodelay(cs);
        if (cfg_.enforceMaxPlayers) {
            bool full = false;
            {
                std::lock_guard<std::mutex> lk(sessionsMtx_);
                std::size_t active = 0;
                for (const auto& entry : sessions_)
                    if (entry.second && !entry.second->closed) ++active;
                full = active >= static_cast<std::size_t>((std::max)(1, cfg_.maxPlayers));
            }
            if (full) {
                std::cout << "[SECURITY] Connection rejected: proxy player limit " << cfg_.maxPlayers << " reached.\n";
                closeSock(cs);
                continue;
            }
        }
        auto session = std::make_shared<Session>();
        session->listenerPort=port;
        {std::lock_guard<std::mutex> lock(sessionsMtx_);session->id = nextId_++;}
        session->client = cs;
        char ipbuf[64]{};
        inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
        session->ip = ipbuf;
        {
            std::lock_guard<std::mutex> admission(admissionMutex_);
            int count=0;{std::lock_guard<std::mutex> lock(sessionsMtx_);for(auto& e:sessions_)if(!e.second->closed&&e.second->ip==session->ip)++count;}
            if(cfg_.connectionLimitPerIP>0 && count>=cfg_.connectionLimitPerIP) {
                disconnectSession(session,cfg_.connectionLimitReason.empty()?cfg_.phrase("connectionRateLimitExceeded"):cfg_.connectionLimitReason);continue;
            }
            if(cfg_.connectionRateLimitPerIP>0) {
                const auto now=steadyMillis();
                for(auto it=connectionTimes_.begin();it!=connectionTimes_.end();) {
                    auto& h=it->second;
                    h.erase(std::remove_if(h.begin(),h.end(),[&](int64_t t){return now-t>cfg_.rateWindowSeconds*1000LL;}),h.end());
                    if(h.empty())it=connectionTimes_.erase(it);else ++it;
                }
                auto& history=connectionTimes_[session->ip];
                if(history.size()>=size_t(cfg_.connectionRateLimitPerIP)) {
                    disconnectSession(session,cfg_.phrase("connectionRateLimitExceeded"));continue;
                }
                history.push_back(now);
            }
            // Reserve the session while admission is locked, across all listeners.
            {std::lock_guard<std::mutex> lock(sessionsMtx_);sessions_[session->id]=session;}
        }
        if(cfg_.socketTimeoutMs>0) {
#ifdef _WIN32
            DWORD timeout=DWORD(cfg_.socketTimeoutMs);setsockopt(cs,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
#else
            timeval timeout{cfg_.socketTimeoutMs/1000,(cfg_.socketTimeoutMs%1000)*1000};setsockopt(cs,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
#endif
        }
        if (auto reason = bans_.reason("ip", session->ip); !reason.empty()) {
            disconnectSession(session, "Бан прокси: " + reason);
            {std::lock_guard<std::mutex> lock(sessionsMtx_);sessions_.erase(session->id);}
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(sessionsMtx_);
            sessions_[session->id] = session;
        }
        std::cout << "[+] New connection #" << session->id << " from " << session->ip << "\n";
        ++workers_;
        std::thread([this, session]{ handleClient(session); --workers_; }).detach();
    }
}

void ProxyServer::handleClient(std::shared_ptr<Session> s) {
    try {
        std::vector<RoutingServer> pool;
        for(const auto& listener:cfg_.listeners)if(listener.port==s->listenerPort)for(auto& name:listener.pool)if(auto t=findServer(name))pool.push_back(*t);
        if(pool.empty()&&defaultServer())pool.push_back(*defaultServer());
        std::map<std::string,int> counts;
        {std::lock_guard<std::mutex> lock(sessionsMtx_);for(auto& e:sessions_)if(!e.second->closed)++counts[e.second->currentServer];}
        std::stable_sort(pool.begin(),pool.end(),[&](const auto& a,const auto& b){return counts[a.name]<counts[b.name];});
        bool connected=false;
        for(auto& target:pool)if(openBackend(s,target,false)){connected=true;break;}
        if(!connected)disconnectSession(s,cfg_.phrase("noServersAvailable"));
        // client -> server pipe (blocking in this thread)
        pipeClientToServer(s);
    } catch (const std::exception& e) {
        std::cerr << "[!] Session #" << s->id << " error: " << e.what() << "\n";
    }
    std::string name = s->username.empty() ? "?" : s->username;
    if (!s->username.empty()) touchLastSeen(s->username);
    std::cout << "[-] Session #" << s->id << " (" << name << ") closed; packets C->S="
              << s->clientPacketCount.load() << ", S->C=" << s->serverPacketCount.load() << "\n";
    disconnectSession(s, "closed");
    {
        std::lock_guard<std::mutex> lk(sessionsMtx_);
        sessions_.erase(s->id);
    }
}

// ---------- open backend ----------
bool ProxyServer::openBackend(std::shared_ptr<Session> s, const RoutingServer& target, bool isSwitch) {
    socket_t bs = connectTarget(target, cfg_.connectTimeoutSec);
    if (bs == INVALID_SOCK) return false;
    if (s->closed) { closeSock(bs); return false; }
    if (isSwitch) {
        auto msg=cfg_.switchMessage;
        for(auto token:{std::string("${name}"),std::string("{server}")}){auto pos=msg.find(token);if(pos!=msg.npos)msg.replace(pos,token.size(),target.name);}
        sendChat(s,msg,255,255,255);applyDebuffs(s);
        s->switchingStartedMs.store(steadyMillis());
    }
    // Invalidate the old S->C reader before closing the previous backend.
    const std::uint64_t generation = s->backendGeneration.fetch_add(1) + 1;
    // Close old backend only after any complete outgoing frame has finished writing.
    {
        std::lock_guard<std::mutex> lock(s->backendWriteMtx);
        s->backendReady = false;
        closeSock(s->backend);
    }
    if (isSwitch) {clearOtherPlayers(s);sendToClient(s,s->entities.takeClearFrames());}
    s->charInjected = false;
    s->sectionSent = false;
    s->spawnSent = false;
    s->tilePackets = 0;
    s->spawnComplete = false;
    s->worldSpawn.reset();

    setNodelay(bs);

    // The socket, handshake, and readiness flag are published atomically with respect to
    // the client packet writer. No gameplay frame may overtake ConnectRequest.
    auto cr = buildConnectRequest(cfg_.terrariaVersion);
    {
        std::lock_guard<std::mutex> lock(s->backendWriteMtx);
        s->backend = bs;
        if (!sendAll(bs, cr)) {
            closeSock(s->backend);
            s->backendReady = false;
            return false;
        }
        s->backendReady = true;
    }
    s->inVoid=false;
    s->currentServer = target.name;
    std::cout << "[LINK] " << (s->username.empty() ? s->ip : s->username)
              << " -> " << target.name << " (" << target.host << ":" << target.port << ")\n";
    // start server->client pipe in background
    ++workers_;
    std::thread([this, s, isSwitch, bs, generation]{ pipeServerToClient(s, isSwitch, bs, generation); --workers_; }).detach();
    return true;
}

void ProxyServer::enterVoid(std::shared_ptr<Session> s) {
    if(!s || s->closed) return;
    if(s->switching || s->warpActive) {sendChat(s,"Дождитесь завершения перехода.");return;}
    if(s->inVoid) {sendChat(s,"Вы уже в пустоте. /back — вернуться.");return;}
    s->inVoid=true; s->voidReturnServer=s->currentServer;
    s->backendGeneration.fetch_add(1);
    {std::lock_guard<std::mutex> lock(s->backendWriteMtx);s->backendReady=false;closeSock(s->backend);}
    clearOtherPlayers(s);
    s->currentServer.clear();
    sendChat(s,cfg_.phrase("youEnteredTheVoid"));
}

void ProxyServer::exportClientRtt(std::shared_ptr<Session> s) {
    if(!cfg_.tcpRttEnabled||!cfg_.exportRtt||s->switching||!s->spawnSent)return;
    auto target=findServer(s->currentServer);if(!target||target->isVanilla)return;
    auto now=steadyMillis();if(now-s->rttSampleMs.load()<cfg_.sampleIntervalMs)return;s->rttSampleMs=now;
    std::optional<double> rtt;{std::lock_guard<std::mutex> lock(s->clientWriteMtx);rtt=nativeRttMs(s->client);}
    std::vector<uint8_t> body{67,6,0,uint8_t(s->playerSlot)};
    int32_t client=rtt?int32_t(*rtt*1000):-1, unknown=-1;
    auto append=[&](auto v){auto ptr=reinterpret_cast<uint8_t*>(&v);body.insert(body.end(),ptr,ptr+sizeof(v));};
    append(client);append(unknown);append(client);
    uint64_t timestamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();append(timestamp);
    sendToBackend(s,framePacket(body));
}

// ---------- switch ----------
void ProxyServer::switchServer(std::shared_ptr<Session> s, const RoutingServer& target) {
    if (!s || s->closed) return;
    if (toLower(s->currentServer) == toLower(target.name)) { sendChat(s, cfg_.phrase("youAreAlreadyInthatDimension")); return; }
    bool expected = false;
    if (!s->switching.compare_exchange_strong(expected, true)) {
        sendChat(s, cfg_.phrase("youNeedToWaitUntilConnected"), 255, 255, 255);
        return;
    }
    // The timestamp is set by openBackend once the handoff begins.
    s->switchingStartedMs.store(0);
    try {
        if (!openBackend(s, target, true)) {
            sendChat(s, cfg_.serverOfflineMessage, 255, 80, 80);
            s->switchingStartedMs.store(0);
            s->switching = false;
            std::cout << "[OFFLINE] " << target.name << " недоступно; переход отменён.\n";
            sendToClient(s, buildStatus("", 0, true));
            if (!s->backendReady) onBackendLost(s, cfg_.serverOfflineMessage, true);
            return;
        }
    } catch (...) {
        s->switchingStartedMs.store(0);
        s->switching = false;
    }
}

void ProxyServer::clearOtherPlayers(std::shared_ptr<Session> s) {
    if (!s || s->client == INVALID_SOCK || s->closed) return;
    std::vector<uint8_t> batch;
    for (int pid = 0; pid < 255; ++pid) {
        if (pid == s->playerSlot) continue;
        auto p = buildPlayerActive(uint8_t(pid), false);
        batch.insert(batch.end(), p.begin(), p.end());
    }
    if (!batch.empty()) sendToClient(s, batch);
}

// ---------- reinject (Python _reinject_character) ----------
void ProxyServer::reinjectCharacter(std::shared_ptr<Session> s, uint8_t newSlot, socket_t backendSocket, std::uint64_t generation) {
    if (!s || backendSocket == INVALID_SOCK || s->closed || s->backendGeneration.load() != generation) return;

    auto send = [&](std::vector<uint8_t> pkt) {
        if (s->closed || s->backendGeneration.load() != generation || backendSocket == INVALID_SOCK) return;
        patchPlayerSlot(pkt, newSlot);
        sendToBackend(s, backendSocket, generation, pkt);

    };

    std::lock_guard<std::mutex> lk(s->cacheMtx);
    if (!s->cachedP4.empty())    send(s->cachedP4);
    if (!s->cachedUuid.empty())  send(s->cachedUuid);
    if (!s->cachedHp.empty())    send(s->cachedHp);
    if (!s->cachedMana.empty())  send(s->cachedMana);
    // Packet 50 is illegal before spawn on strict backends; flush it only after packet 12.
    for (auto& kv : s->cachedSlots) send(kv.second);

    auto req = buildRequestWorldData();
    if (s->backendGeneration.load() == generation) sendToBackend(s, backendSocket, generation, req);
    s->charInjected = true;
}

void ProxyServer::sendSectionsAndSpawn(std::shared_ptr<Session> s, socket_t backendSocket, std::uint64_t generation) {
    if (!s || s->sectionSent || backendSocket == INVALID_SOCK || s->closed || s->backendGeneration.load() != generation) return;
    if (!s->charInjected) return;

    s->sectionSent = true;
    auto sec = buildGetSection(-1, -1);
    if (s->backendGeneration.load() != generation) return;
    sendToBackend(s, backendSocket, generation, sec);
}

// ---------- debuff on switch (Python manager.handle_debuff_on_switch) ----------
void ProxyServer::applyDebuffs(std::shared_ptr<Session> s) {
    if (!cfg_.debuffOnSwitch) return;
    if (!s || s->closed || s->client == INVALID_SOCK || s->playerSlot < 0) return;

    const auto generation = s->backendGeneration.load();
    for (int bid : cfg_.debuffIds) {
        auto pkt = buildAddPlayerBuff(uint8_t(s->playerSlot), uint16_t(bid), cfg_.debuffSeconds * 60);
        sendToClient(s, pkt, generation);
    }
}

// ---------- fallback to lobby ----------
void ProxyServer::onBackendLost(std::shared_ptr<Session> s, const std::string& reason, bool force, const std::vector<uint8_t>& kickFrame) {
    if (!s || s->closed) return;
    if (s->inVoid || (s->switching && !force)) return;

    if(!kickFrame.empty()) {
        bool close=cfg_.kickPolicy=="always";
        if(cfg_.kickPolicy=="onKickReasonPrefix")for(auto& prefix:cfg_.kickPrefixes)if(reason.rfind(prefix,0)==0)close=true;
        if(close){disconnectSession(s,reason);return;}
    }
    auto* lobby = defaultServer();
    std::string text = reason.empty()
        ? (cfg_.dimensionDroppedMessage.empty() ? cfg_.kickMessage : cfg_.dimensionDroppedMessage)
        : reason;

    sendChat(s,cfg_.phrase("dimensionDisconnectedYou"),68,212,82);
    if (kickFrame.size() >= 5) {
        // Keep NetworkText mode and substitutions intact so the client can localize them.
        std::vector<uint8_t> body{82,1,0,255};
        body.insert(body.end(),kickFrame.begin()+3,kickFrame.end());
        body.insert(body.end(),{255,180,100});
        sendToClient(s,framePacket(body));
    } else sendChat(s," " + text,255,180,100);
    if (!lobby || (!s->currentServer.empty() && toLower(s->currentServer)==toLower(lobby->name))) {
        disconnectSession(s,text);return;
    }

    std::cout << "[FALLBACK] " << (s->username.empty() ? s->ip : s->username)
              << " -> " << lobby->name << " (" << text << ")\n";

    s->switchingStartedMs.store(0);
    s->switching = true;
    if (!openBackend(s, *lobby, true)) {
        disconnectSession(s, cfg_.serverOfflineMessage);
    }
}

// ---------- client -> server pipe ----------
void ProxyServer::pipeClientToServer(std::shared_ptr<Session> s) {
    TerrariaFramer framer;
    uint8_t buf[65536];

    while (!s->closed && s->client != INVALID_SOCK) {
#ifdef _WIN32
        int n = recv(s->client, (char*)buf, sizeof(buf), 0);
#else
        ssize_t n = recv(s->client, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;

        std::vector<std::vector<uint8_t>> frames;
        framer.feed(buf, size_t(n), frames);

        for (auto& frame : frames) {
            if (frame.size() < 3) continue;

            const uint8_t observedPacket = frame[2];
            Logger::instance().packet("C->S", s->id, ++s->clientPacketCount,
                                      observedPacket, packetTypeName(observedPacket), frame);

            exportClientRtt(s);
            uint8_t pkt = observedPacket;
            if (pkt == 67 || pkt == static_cast<uint8_t>(PacketType::ConnectRequest)) continue;
            if(cfg_.blockInvis && pkt==55 && frame.size()>=6 && (uint16_t(frame[4])|(uint16_t(frame[5])<<8))==10) continue;
            if (pkt == 50) {
                if(cfg_.blockInvis && frame.size()>=6) {
                    std::vector<uint16_t> buffs;
                    for(size_t i=4;i+1<frame.size();i+=2) {auto id=uint16_t(uint16_t(frame[i])|(uint16_t(frame[i+1])<<8));if(!id)break;if(id!=10)buffs.push_back(id);}
                    frame=buildSyncPlayerBuffs(frame[3],buffs);
                }
                if(s->switching)continue;
                std::lock_guard<std::mutex> lock(s->cacheMtx);
                s->cachedBuffs = frame;
                if (!s->spawnSent) continue;
            }
            if (s->switching) continue;
            if (s->inVoid && pkt == static_cast<uint8_t>(PacketType::Ping)) {sendToClient(s,frame);continue;}


            if (pkt == 27 || pkt == 29) s->entities.observe(frame);

            // World edits and chest operations are owned by Terraria/TShock, not by the
            // proxy or optional extensions. Keep the frame byte-for-byte intact, except
            // SyncTilePicking (125), whose first payload byte is the player's slot when
            // moving between backend servers. This path runs before extensions and command
            // filters, so a plugin cannot accidentally consume a placement/chest action.
            if (isWorldModifyPacket(pkt) || isChestInteractionPacket(pkt)) {
                if (pkt == static_cast<uint8_t>(PacketType::SyncTilePicking) && s->playerSlot >= 0)
                    patchPlayerSlot(frame, static_cast<uint8_t>(s->playerSlot));
                if (s->backendReady && !sendToBackend(s, frame)) break;
                continue;
            }

            // Extensions may consume non-world packets; all packets are logged above.
            if (ext_ && ext_->handleClientPacket(s, frame)) continue;

            // cache character data
            {
                std::lock_guard<std::mutex> lk(s->cacheMtx);
                if (pkt == static_cast<uint8_t>(PacketType::PlayerInfo)) {
                    if(!s->username.empty() && cfg_.nameChangeMode=="rewrite")rewritePlayerName(frame,s->username);
                    s->cachedP4 = frame;
                    if (s->username.empty()) {
                        auto name = extractPlayerName(frame.data(), frame.size());
                        if (name) {
                            if (auto reason = bans_.reason("name", *name); !reason.empty()) {
                                disconnectSession(s, "Бан прокси: " + reason); return;
                            }
                            if (cfg_.antiNickEnabled) {
                                if (name->size() < static_cast<std::size_t>(cfg_.minNickLength) ||
                                    name->size() > static_cast<std::size_t>(cfg_.maxNickLength)) {
                                    disconnectSession(s, "Ник должен содержать от " + std::to_string(cfg_.minNickLength) +
                                        " до " + std::to_string(cfg_.maxNickLength) + " символов.");
                                    return;
                                }
                                bool valid = true;
                                for (unsigned char c : *name) {
                                    const bool asciiAlpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                                    const bool digit = c >= '0' && c <= '9';
                                    if (!(asciiAlpha || digit || c == '_' || c == '-')) { valid = false; break; }
                                }
                                if (!valid) {
                                    disconnectSession(s, "Недопустимый ник. Разрешены A-Z, a-z, 0-9, _ и -.");
                                    return;
                                }
                                const std::string normalizedName = toLower(*name);
                                bool reserved = false;
                                for (const auto& item : cfg_.reservedNicknames)
                                    if (normalizedName == toLower(item)) { reserved = true; break; }
                                if (reserved) {
                                    disconnectSession(s, "Этот ник зарезервирован администрацией. Выберите другой.");
                                    return;
                                }
                            } else if (name->empty() || name->size() > 24) {
                                disconnectSession(s, "Некорректная длина ника.");
                                return;
                            }
                            // duplicate check
                            bool dup = false;
                            {
                                std::lock_guard<std::mutex> lk2(sessionsMtx_);
                                for (auto& kv : sessions_) {
                                    if (kv.second->id != s->id && !kv.second->closed &&
                                        toLower(kv.second->username) == toLower(*name)) { dup = true; break; }
                                }
                            }
                            if (dup) {
                                auto msg=cfg_.phrase("nameAlreadyOnServer");auto pos=msg.find("${name}");if(pos!=std::string::npos)msg.replace(pos,7,*name);disconnectSession(s,msg);
                                return;
                            }
                            s->username = *name;
                            touchLastSeen(*name);
                            std::cout << "[NAME] Session #" << s->id << " = " << *name << "\n";
                        }
                    }
                } else if (pkt == static_cast<uint8_t>(PacketType::PlayerInventorySlot) && frame.size() >= 6) {
                    int16_t slotId = 0;
                    std::memcpy(&slotId, frame.data() + 4, 2);
                    s->cachedSlots[slotId] = frame;
                } else if (pkt == static_cast<uint8_t>(PacketType::PlayerHP)) {
                    s->cachedHp = frame;
                } else if (pkt == static_cast<uint8_t>(PacketType::PlayerMana)) {
                    s->cachedMana = frame;
                } else if (pkt == static_cast<uint8_t>(PacketType::UpdatePlayerBuff)) {
                    s->cachedBuffs = frame;
                } else if (pkt == static_cast<uint8_t>(PacketType::ClientUUID)) {
                    s->cachedUuid = frame;
                } else if (pkt == static_cast<uint8_t>(PacketType::SpawnPlayer)) {
                    s->cachedSpawn = frame;
                }
            }

            // UpdatePlayer -> warp check
            if (pkt == static_cast<uint8_t>(PacketType::UpdatePlayer) && warps_) {
                warps_->handleUpdatePlayer(s, frame.data(), frame.size());
            }

            // Chat parsing, command dispatch and per-player anti-spam.
            if (pkt == static_cast<uint8_t>(PacketType::LoadNetModule)) {
                auto text = extractChatText(frame.data(), frame.size());
                if (text && !text->empty()) {
                    if (cfg_.antiSpamEnabled) {
                        const std::int64_t now = steadyMillis();
                        if (s->chatWindowStartMs == 0 || now - s->chatWindowStartMs >= 10000) {
                            s->chatWindowStartMs = now;
                            s->chatMessagesInWindow = 0;
                        }
                        const bool duplicateFlood = cfg_.duplicateChatCooldownMs > 0 &&
                            *text == s->lastChatMessage && now - s->lastChatMessageMs < cfg_.duplicateChatCooldownMs;
                        const bool rateFlood = s->chatMessagesInWindow >=
                            static_cast<unsigned>(cfg_.maxChatMessagesPer10Seconds);
                        if (duplicateFlood || rateFlood) {
                            if (now - s->lastAntiSpamNoticeMs >= 3000) {
                                s->lastAntiSpamNoticeMs = now;
                                sendChat(s, "[c/FFAA55:Антиспам: сообщения отправляются слишком часто. Подождите немного.]",
                                         255, 170, 85);
                                std::cout << "[SECURITY] AntiSpam suppressed chat from "
                                          << (s->username.empty() ? s->ip : s->username) << "\n";
                            }
                            continue;
                        }
                        ++s->chatMessagesInWindow;
                        s->lastChatMessage = *text;
                        s->lastChatMessageMs = now;
                    }
                    if (((*text)[0] == '/' || (*text)[0] == '.') &&
                        CommandHandler::handle(*this, s, *text)) continue;
                }
            }

            // rewrite ConnectRequest version
            if (pkt == static_cast<uint8_t>(PacketType::ConnectRequest)) {
                frame = buildConnectRequest(cfg_.terrariaVersion);
            } else if (s->playerSlot >= 0) {
                patchPlayerSlot(frame, uint8_t(s->playerSlot));
            }

            if (s->backendReady && !sendToBackend(s, frame)) break;
            if(pkt==4 || pkt==12)sendRealIp(s,s->backend,s->backendGeneration.load());
            if (pkt == 12) {
                s->spawnSent = true;
                std::lock_guard<std::mutex> lock(s->cacheMtx);
                if (!s->cachedBuffs.empty()) {auto buffs=s->cachedBuffs;patchPlayerSlot(buffs,uint8_t(s->playerSlot));sendToBackend(s,buffs);}
            }
        }
    }
    // client closed -> close everything
    {
        std::lock_guard<std::mutex> lock(s->clientWriteMtx);
        closeSock(s->client);
    }
    s->closed = true;
    {
        std::lock_guard<std::mutex> lock(s->backendWriteMtx);
        s->backendReady = false;
        closeSock(s->backend);
    }
}

// ---------- server -> client pipe ----------
void ProxyServer::pipeServerToClient(std::shared_ptr<Session> s, bool isSwitch, socket_t backendSocket, std::uint64_t generation) {
    TerrariaFramer framer;
    uint8_t buf[65536];
    bool reinjected = false;

    bool loadingDone = false;
    bool worldInfoLogged = false;

    try {
        while (!s->closed && backendSocket != INVALID_SOCK && s->backendGeneration.load() == generation) {
            if (isSwitch && !loadingDone) {
                fd_set reads; FD_ZERO(&reads); FD_SET(backendSocket, &reads);
                timeval wait{1,0};
                int ready = select(static_cast<int>(backendSocket+1), &reads, nullptr, nullptr, &wait);
                if (ready == 0) {
                    if (steadyMillis() - s->switchingStartedMs.load() > 30000) break;
                    continue;
                }
                if (ready < 0) break;
            }
#ifdef _WIN32
            int n = recv(backendSocket, (char*)buf, sizeof(buf), 0);
#else
            ssize_t n = recv(backendSocket, buf, sizeof(buf), 0);
#endif
            if (n <= 0) break;
            if (s->closed || s->backendGeneration.load() != generation) break;

            std::vector<std::vector<uint8_t>> frames;
            framer.feed(buf, size_t(n), frames);

            std::vector<uint8_t> out;
            for (auto& frame : frames) {
                if (s->closed || s->backendGeneration.load() != generation) break;
                if (frame.size() < 3) continue;

                const uint8_t observedPacket = frame[2];
                Logger::instance().packet("S->C", s->id, ++s->serverPacketCount,
                                          observedPacket, packetTypeName(observedPacket), frame);

                if (ext_ && ext_->handleServerPacket(s, frame)) continue;

                uint8_t pkt = observedPacket;
                if(pkt==50 && frame.size()>=6 && frame[3]==s->playerSlot) {
                    std::lock_guard<std::mutex> lock(s->cacheMtx);
                    s->cachedBuffs=frame;
                }
                if(pkt==4 && frame.size()>=12 && frame[3]==s->playerSlot && cfg_.nameChangeMode=="rewrite") {
                    std::lock_guard<std::mutex> lock(s->cacheMtx);
                    bool allowed=false;for(auto& name:cfg_.nameChangeExclusions)if(toLower(name)==toLower(s->currentServer))allowed=true;
                    if(allowed){if(auto name=extractPlayerName(frame.data(),frame.size()))s->username=*name;}
                    else if(!s->username.empty())rewritePlayerName(frame,s->username);
                    s->cachedP4=frame;
                }

                if(pkt==67) {
                    // REXT receiver acknowledgement: uint16 0x5258 + applied IP.
                    if(frame.size()>=6 && frame[3]==0x58 && frame[4]==0x52) {
                        size_t offset=5;auto applied=readCsharpString(frame.data(),frame.size(),offset);
                        if(offset==frame.size() && applied==s->ip && s->realIpAckGeneration.exchange(generation)!=generation)
                            std::cout<<"[REALIP] Applied: "<<s->ip<<" -> "<<s->currentServer<<" session="<<s->id<<"\n";
                        continue;
                    }
                    if(frame.size()>=6 && frame[3]==2 && frame[4]==0) {
                        size_t offset=5;auto name=readCsharpString(frame.data(),frame.size(),offset);
                        if(offset>frame.size()||name.empty()) {std::cout<<"[BRIDGE] Invalid switch request\n";continue;}
                        if(auto target=findServer(name)) {
                            if(!out.empty()){sendToClient(s,out,generation);out.clear();}
                            switchServer(s,*target);
                            if(s->backendGeneration.load()!=generation)return;
                        } else sendChat(s,cfg_.phrase("noServersAvailable"));
                    }
                    // Backend control packets are never forwarded to Terraria clients.
                    continue;
                }
                if (pkt == static_cast<uint8_t>(PacketType::Disconnect)) {
                    auto reason = extractDisconnectReason(frame.data(), frame.size());
                    std::string r = reason.value_or(cfg_.kickMessage);
                    std::cout << "[KICK] " << s->currentServer << " -> " << s->username << ": " << r
                              << " | spawnSent=" << s->spawnSent << " charInjected=" << s->charInjected << "\n";
                    onBackendLost(s, r, true, frame);
                    return;
                }

                if (pkt == static_cast<uint8_t>(PacketType::CompleteConnectionAndSpawn))
                    s->spawnComplete = true;

                if (pkt == static_cast<uint8_t>(PacketType::SendSection) ||
                    pkt == static_cast<uint8_t>(PacketType::SectionTileFrame) ||
                    pkt == static_cast<uint8_t>(PacketType::SendTileSquare))
                    s->tilePackets++;

                // WorldInfo: вытащить spawn + секции
                if (pkt == static_cast<uint8_t>(PacketType::WorldInfo)) {
                    int sx = 0, sy = 0;
                    if (extractWorldSpawn(frame.data(), frame.size(), sx, sy)) {
                        s->worldSpawn = std::make_pair(sx, sy);
                        // WorldInfo also carries ongoing world updates. Log only the
                        // first valid packet per backend connection; always forward it.
                        if (!worldInfoLogged) {
                            worldInfoLogged = true;
                            std::cout << "[WORLD] session=" << s->id << " generation=" << generation
                                      << " spawn=" << sx << "," << sy << "\n";
                        }
                    }
                    if (isSwitch && s->charInjected && !s->sectionSent) {
                        out.insert(out.end(), frame.begin(), frame.end());
                        sendSectionsAndSpawn(s, backendSocket, generation);
                        continue;
                    }
                }

                // Во время switch — не прокидывать «лишний» чат от бэкенда
                // (уменьшает двойные «присоединился / залогинились»)
                if (isSwitch && !loadingDone) {
                    if (pkt == static_cast<uint8_t>(PacketType::ChatMessage))
                        continue;
                    // LoadNetModule server text often used for join announcements
                    if (pkt == static_cast<uint8_t>(PacketType::LoadNetModule) && frame.size() >= 5) {
                        // module id little-endian at 3..4 — 1 = text
                        uint16_t mod = uint16_t(frame[3]) | (uint16_t(frame[4]) << 8);
                        if (mod == 1) continue;
                    }
                }

                // ContinueConnecting → slot + reinject
                if (pkt == static_cast<uint8_t>(PacketType::ContinueConnecting) && frame.size() >= 4) {
                    uint8_t newSlot = frame[3];
                    if(isSwitch && s->playerSlot>=0 && s->playerSlot!=int(newSlot))
                        sendToClient(s,buildPlayerActive(uint8_t(s->playerSlot),false),generation);
                    s->playerSlot = int(newSlot);
                    sendRealIp(s,backendSocket,generation);
                    if (isSwitch && !reinjected) {
                        sendToClient(s, frame, generation);
                        reinjected = true;
                        reinjectCharacter(s, newSlot, backendSocket, generation);
                        sendRealIp(s,backendSocket,generation);
                        continue;
                    }
                }

                if (isSwitch && !loadingDone && pkt == static_cast<uint8_t>(PacketType::CompleteConnectionAndSpawn)) {
                    if (!out.empty()) { sendToClient(s, out, generation); out.clear(); }
                    auto spawn = buildSpawnPlayer(uint8_t(s->playerSlot), s->worldSpawn ? s->worldSpawn->first : -1,
                                                  s->worldSpawn ? s->worldSpawn->second : -1);
                    sendToBackend(s, backendSocket, generation, spawn);
                    {
                        std::lock_guard<std::mutex> lock(s->cacheMtx);
                        if(!s->cachedBuffs.empty()) {
                            auto buffs=s->cachedBuffs;patchPlayerSlot(buffs,uint8_t(s->playerSlot));
                            sendToBackend(s,backendSocket,generation,buffs);
                        }
                    }
                    sendToClient(s, spawn, generation);
                    sendToClient(s, frame, generation);
                    sendToClient(s, framePacket({static_cast<uint8_t>(PacketType::Ping)}), generation);
                    sendRealIp(s,backendSocket,generation);
                    s->spawnSent = true; loadingDone = true;
                    s->switchingStartedMs = 0; s->switching = false;


                    std::cout << "[SWITCH] " << s->username << " -> " << s->currentServer << " READY\n";
                    continue;
                }

                out.insert(out.end(), frame.begin(), frame.end());
            }

            if (!out.empty() && s->client != INVALID_SOCK && !s->closed && s->backendGeneration.load() == generation) {
                if (!sendToClient(s, out, generation)) break;
            }

        }
    } catch (...) {}

    // A retired backend reader must not mark the newer connection as failed.
    {
        std::lock_guard<std::mutex> lock(s->backendWriteMtx);
        if (s->backendGeneration.load() != generation) return;
        s->backendReady = false;
    }
    if (!s->closed) onBackendLost(s, cfg_.phrase("dimensionDropped"), true);
}

void ProxyServer::consoleLoop() {
    auto printUsage = [] {
        std::cout << "\x1b[93m"
                     "hud — панель                  list — игроки                  dim — серверы\n"
                     "move — переместить            kick — отключить               ban — бан ника\n"
                     "banip — бан IP                unban — снять бан ника         unbanip — снять бан IP\n"
                     "bans — список банов           warps — пластины               plugins — расширения\n"
                     "modules — модули              manager — права                quit — остановить\n"
                     "\x1b[0m"
                     "move <nick> <server>; kick/ban <nick> [reason]; banip <IP> [reason]\n"
                     "warps add <source> <target> <x> <y> <w> <h>; warps list; warps del <id>\n"
                     "plugins list; plugins enable/disable <name>; manager grant <nick> <rule> <duration>\n";
    };
    auto showRules = [this](const std::string& filter) {
        auto entries = playerRules_.list(filter);
        if (entries.empty()) {
            std::cout << "[MANAGER] No active rules";
            if (!filter.empty()) std::cout << " for " << filter;
            std::cout << ".\n";
            return;
        }
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        std::cout << "[MANAGER] Active temporary rules:\n";
        for (const auto& entry : entries) {
            std::cout << "  " << entry.player << "  rule=" << entry.rule
                      << "  remaining=" << PlayerRuleManager::formatRemaining(entry.expiresAt - now) << "\n";
        }
    };
    auto findOnline = [this](const std::string& name) -> std::shared_ptr<Session> {
        const std::string key = toLower(name);
        std::lock_guard<std::mutex> lk(sessionsMtx_);
        for (auto& kv : sessions_) {
            const auto& s = kv.second;
            if (s && !s->closed && !s->username.empty() && toLower(s->username) == key) return s;
        }
        return {};
    };

    auto alive=consoleAlive_;
    std::string line;
    while (alive->load()) {
        // Use stdio so the input prompt is visible even while std::cout is routed through the async logger.
        std::fputs("\x1b[91mREXT\x1b[31m::\x1b[97mPROXY\x1b[90m // \x1b[92mONLINE\x1b[31m > \x1b[0m", stdout);
        std::fflush(stdout);
        if (!readConsoleLine(line,*alive)) break;
        if(!alive->load())return;
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        cmd = toLower(cmd);

        if (cmd == "quit" || cmd == "exit" || cmd == "stop") {
            stop();
            return;
        } else if (cmd == "ip" || cmd == "realip") {
            std::string nick;iss>>nick;auto session=findOnline(nick);
            if(!session){std::cout<<"[REALIP] Usage: ip <online player>\n";continue;}
            bool confirmed=session->realIpAckGeneration.load()==session->backendGeneration.load() && session->backendGeneration.load()!=0 && !session->inVoid;
            std::cout<<"[REALIP] "<<session->username<<" client="<<session->ip<<" backend="<<session->currentServer
                     <<" applied="<<(confirmed?"YES":"UNCONFIRMED (check backend receiver / isVanilla)")<<"\n";
        } else if (cmd == "help" || cmd == "?") {
            printUsage();
        } else if (cmd == "status" || cmd == "online" || cmd == "hud" || cmd == "menu") {
            int online = 0;
            {
                std::lock_guard<std::mutex> lk(sessionsMtx_);
                for (const auto& kv : sessions_)
                    if (kv.second && !kv.second->closed && !kv.second->username.empty()) ++online;
            }
            Logger::instance().showBanner();
            printUsage();
            std::cout << "[MENU] \x1b[93mlist, dim, move, kick, ban, banip, bans, warps, modules, plugins, help\x1b[0m\n";
            std::cout << "\x1b[91m[CORE]\x1b[0m state=\x1b[92mONLINE\x1b[0m"
                      << " protocol=326 listen=0.0.0.0:" << cfg_.listenPort << "\n"
                      << "\x1b[91m[SESSIONS]\x1b[0m online=" << online << "/" << cfg_.maxPlayers
                      << " backends=" << cfg_.servers.size() << "\n"
                      << "\x1b[91m[GUARD]\x1b[0m AntiNick=" << (cfg_.antiNickEnabled ? "ON" : "OFF")
                      << " AntiSpam=" << (cfg_.antiSpamEnabled ? "ON" : "OFF")
                      << " MaxPlayers=" << (cfg_.enforceMaxPlayers ? "ON" : "OFF") << "\n";
        } else if (cmd == "list" || cmd == "players") {
            int online = 0;
            std::lock_guard<std::mutex> lk(sessionsMtx_);
            std::cout << "[PLAYERS] Connected sessions:\n";
            for (const auto& kv : sessions_) {
                const auto& session = kv.second;
                if (!session || session->closed) continue;
                ++online;
                std::cout << "  #" << session->id << " "
                          << (session->username.empty() ? "<joining>" : session->username)
                          << " @ " << (session->currentServer.empty() ? "<connecting>" : session->currentServer)
                          << " | " << session->ip << " | C->S " << session->clientPacketCount.load()
                          << " / S->C " << session->serverPacketCount.load() << "\n";
            }
            std::cout << "[PLAYERS] Sessions: " << online << "\n";
        } else if (cmd == "servers" || cmd == "server" || cmd == "dim" || cmd == "dimensions") {
            std::cout << "[ROUTING] Configured Terraria worlds:\n";
            for (const auto& server : cfg_.servers) {
                int count = 0;
                {
                    std::lock_guard<std::mutex> lk(sessionsMtx_);
                    for (const auto& kv : sessions_) {
                        const auto& session = kv.second;
                        if (session && !session->closed && toLower(session->currentServer) == toLower(server.name)) ++count;
                    }
                }
                auto probe = connectTarget(server, cfg_.connectTimeoutSec);
                bool available = probe != INVALID_SOCK; closeSock(probe);
                std::cout << (available ? "  [TCP OK] " : "  [OFFLINE] ") << server.name << " -> " << server.host << ':' << server.port
                          << " | players=" << count << "\n";
            }
        } else if (cmd == "warps") {
            std::string args;std::getline(iss,args);warps_->console(args);
        } else if (cmd == "modules") {
            std::cout << "[MODULES] Routing=ON, Void=ON, NativeTcpRtt=ON, Bans=ON, Warpplates=ON\n"
                      << "[MODULES] AntiNick=" << cfg_.antiNickEnabled << ", AntiSpam=" << cfg_.antiSpamEnabled << "\n";
            ext_->printList();
        } else if (cmd == "plugins" || cmd == "extensions") {
            std::string action, name; iss >> action >> std::quoted(name);
            if (action.empty() || action == "list") ext_->printList();
            else if ((action == "enable" || action == "disable") && !name.empty()) {
                if (!ext_->setEnabled(name, action == "enable")) std::cout << "[EXT] Расширение не найдено.\n";
            } else std::cout << "[EXT] plugins [list | enable <name> | disable <name>]\n";
        } else if (cmd == "ban" || cmd == "banip" || cmd == "unban" || cmd == "unbanip" || cmd == "bans") {
            std::string value, reason; iss >> std::quoted(value); std::getline(iss >> std::ws, reason);
            try {
                if (cmd == "bans") {
                    for (auto& e : bans_.list()) std::cout << "[BAN] " << e.first << " : " << e.second << "\n";
                } else if (value.empty()) std::cout << "[ERROR] Укажите ник или IP.\n";
                else if (cmd == "unban" || cmd == "unbanip") {
                    std::cout << (bans_.remove(cmd == "unban" ? "name" : "ip",value) ? "[OK] Бан снят.\n" : "[WARN] Бан не найден.\n");
                } else {
                    if (cmd == "banip") { in_addr addr{}; if(inet_pton(AF_INET,value.c_str(),&addr)!=1) {std::cout << "[ERROR] Нужен IPv4.\n";continue;} }
                    if (reason.empty()) reason="Заблокирован администратором.";
                    bans_.set(cmd == "ban" ? "name" : "ip",value,reason);
                    std::vector<std::shared_ptr<Session>> targets;
                    { std::lock_guard<std::mutex> lock(sessionsMtx_);
                      for(auto& e:sessions_) if(cmd=="ban" ? toLower(e.second->username)==toLower(value) : e.second->ip==value) targets.push_back(e.second); }
                    for(auto& session:targets) disconnectSession(session,"Бан прокси: "+reason);
                    std::cout << "[OK] Бан сохранён: " << value << "\n";
                }
            } catch(const std::exception& e) {std::cout << "[ERROR] " << e.what() << "\n";}
        } else if (cmd == "kick") {
            std::string name, reason;
            iss >> std::quoted(name);
            std::getline(iss, reason);
            if (!reason.empty() && reason[0] == ' ') reason.erase(0, 1);
            if (name.empty()) { std::cout << "Usage: kick <nick> [reason]\n"; continue; }
            if (reason.empty()) reason = cfg_.kickMessage;
            auto session = findOnline(name);
            if (!session) std::cout << "[WARN] Player not found: " << name << "\n";
            else {
                disconnectSession(session, reason);
                std::cout << "[OK] Kicked " << (session->username.empty() ? name : session->username) << "\n";
            }
        } else if (cmd == "move" || cmd == "tp") {
            std::string player, serverName;
            iss >> std::quoted(player) >> std::quoted(serverName);
            if (player.empty() || serverName.empty()) { std::cout << "Usage: move <nick> <server>\n"; continue; }
            const auto* targetPtr = findServer(serverName);
            if (!targetPtr) { std::cout << "[ERROR] Unknown server: " << serverName << "\n"; continue; }
            RoutingServer target = *targetPtr;
            auto session = findOnline(player);
            if (!session) std::cout << "[WARN] Player not found: " << player << "\n";
            else {
                ++workers_;
                std::thread([this, session, target]{ switchServer(session, target); --workers_; }).detach();
                std::cout << "[OK] Routing " << session->username << " -> " << target.name << "\n";
            }
        } else if (cmd == "manager" || cmd == "grant" || cmd == "revoke" || cmd == "rules") {
            std::string sub;
            if (cmd == "manager") iss >> sub;
            else sub = cmd;
            sub = toLower(sub);
            if (sub == "help" || sub.empty()) {
                std::cout << "[MANAGER] manager grant <nick> <rule> <duration>\n"
                             "          manager revoke <nick> <rule>\n"
                             "          manager list [nick]\n"
                             "Rules in use: warp, all | durations: 30m, 2h, 1d, 1w (max 365 days)\n";
            } else if (sub == "grant") {
                std::string player, rule, duration;
                iss >> player >> rule >> duration;
                const auto seconds = PlayerRuleManager::parseDuration(duration);
                if (player.empty() || rule.empty() || !seconds) {
                    std::cout << "[ERROR] Usage: manager grant <nick> <rule> <duration> (examples: warp 2h, warp 1d)\n";
                    continue;
                }
                if (!playerRules_.grant(player, rule, *seconds)) {
                    std::cout << "[ERROR] Invalid nickname/rule or duration. Nick: 2-20 chars A-Z 0-9 _ -\n";
                    continue;
                }
                std::cout << "[OK] Rule '" << toLower(rule) << "' granted to " << player
                          << " for " << PlayerRuleManager::formatRemaining(*seconds)
                          << " | saved to " << playerRules_.path() << "\n";
            } else if (sub == "revoke") {
                std::string player, rule;
                iss >> player >> rule;
                if (player.empty() || rule.empty()) {
                    std::cout << "[ERROR] Usage: manager revoke <nick> <rule>\n";
                    continue;
                }
                if (playerRules_.revoke(player, rule)) std::cout << "[OK] Removed rule '" << rule << "' from " << player << "\n";
                else std::cout << "[WARN] No such rule for " << player << "\n";
            } else if (sub == "list") {
                std::string player;
                iss >> player;
                showRules(player);
            } else {
                std::cout << "[ERROR] Unknown manager action: " << sub << " (type manager help)\n";
            }
        } else {
            std::cout << "[WARN] Unknown command: " << cmd << " (type help)\n";
        }
    }
}
