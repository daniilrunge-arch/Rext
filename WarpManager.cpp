#include "WarpManager.hpp"
#include "ProxyServer.hpp"
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cmath>

WarpManager::WarpManager(ProxyServer* proxy) : proxy_(proxy) {

}

void WarpManager::setWarps(const std::vector<WarpDef>& defs) {
    std::lock_guard<std::mutex> lock(mutex_);
    warps_.clear();
    for (auto& d : defs)
        warps_.push_back({d.x, d.y, d.w, d.h, d.target, d.source});
}

void WarpManager::handleUpdatePlayer(std::shared_ptr<Session> session,
                                     const uint8_t* frame, size_t size) {
    if (!session || session->currentServer.empty() || size < 17) return;

    std::string cur = session->currentServer;
    for (auto& c : cur)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    float px = 0.f, py = 0.f;
    std::memcpy(&px, frame + 9, 4);
    std::memcpy(&py, frame + 13, 4);

    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    bool cancelled = false;
    bool denied = false;
    bool startWarp = false;
    bool onPlate = false;
    WarpPlate selected{};

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Determine whether the player is actually standing on a configured plate first.
        // Permission checks must never run for ordinary movement anywhere else in Lobby.
        for (const auto& warp : warps_) {
            std::string source=warp.sourceServer; for(auto& c:source)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (source==cur && warp.contains(px, py)) {
                onPlate = true;
                selected = warp;
                break;
            }
        }

        auto active = activeWarps_.find(session->id);
        if (active != activeWarps_.end()) {
            if (!active->second.warp.contains(px, py) || active->second.warp.sourceServer != session->currentServer) {
                activeWarps_.erase(active);

                session->warpActive = false;
                cancelled = true;
            } else {
                // The same countdown is already running; do not trigger it again.
                return;
            }
        }

        if (!onPlate) {
            deniedPlayers_.erase(session->id);
            if (!cancelled) return;
        }

        if (cancelled) {
            deniedPlayers_.erase(session->id);
        } else if (session->switching || session->warpActive) {
            return;
        } else if (cooldowns_.count(session->id) && now - cooldowns_[session->id] < 6.0) {
            return;
        } else if (proxy_->getConfig().requireWarpRule &&
                   !proxy_->hasPlayerRule(session->username, "warp")) {
            // Remember that this visit was already denied. The notice is reset when the
            // player walks off all plates, so movement packets cannot spam chat.
            if (deniedPlayers_.insert(session->id).second) denied = true;
        } else {
            deniedPlayers_.erase(session->id);
            cooldowns_[session->id] = now;

            session->warpActive = true;
            activeWarps_[session->id] = {selected, false};
            startWarp = true;
        }
    }

    if (cancelled) {
        proxy_->sendChat(session,
            "[i:4076][c/d62ef0:[Измерения][c/d62ef0:]] [c/f88efa:Телепортация отменена (вы сошли с варп-пластины).]",
            255, 255, 255);
        std::cout << "[WARP] " << (session->username.empty() ? session->ip : session->username)
                  << " stepped off plate -> cancelled\n";
        return;
    }
    if (denied) {
        proxy_->sendChat(session,
            "[c/FF5555:Доступ к варпу закрыт. Обратитесь к администрации за временным правилом warp.]",
            255, 255, 255);
        return;
    }
    if (startWarp) {
        std::cout << "[WARP] " << session->username << " entered plate -> " << selected.targetServer << "\n";
        std::thread(&WarpManager::doWarpCountdown, this, session, selected).detach();
    }
}

void WarpManager::doWarpCountdown(std::shared_ptr<Session> session, WarpPlate warp) {
    for (int seconds = 5; seconds >= 1; --seconds) {
        bool active = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = activeWarps_.find(session->id);
            active = it != activeWarps_.end() && !it->second.cancelled;
        }
        if (session->closed) return;
        if (!active) {

            session->warpActive = false;
            return;
        }
        std::string msg =
            "[i:4076][c/d62ef0:[Измерения][c/d62ef0:]] [c/8efad2:Телепортация в] "
            + warp.targetServer + " [c/8efad2:через " + std::to_string(seconds) + "...]";
        proxy_->sendChat(session, msg, 255, 255, 255);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    bool valid = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = activeWarps_.find(session->id);
        if (it != activeWarps_.end() && !it->second.cancelled) {
            activeWarps_.erase(it);
            valid = true;
        }
    }

    session->warpActive = false;
    if (!valid || session->closed || session->inVoid || session->switching || session->currentServer != warp.sourceServer) return;

    auto* t = proxy_->findServer(warp.targetServer);
    if (!t) {
        proxy_->sendChat(session,
            "[i:4076][c/d62ef0:[Измерения][c/d62ef0:]] [c/FF5555:Измерение не найдено.]",
            255, 80, 80);
        return;
    }

    proxy_->switchServer(session, *t);
}

void WarpManager::loadSaved() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream f("warps.db"); if(!f) return;
    std::vector<WarpPlate> loaded; WarpPlate w;
    while(f >> std::quoted(w.sourceServer) >> std::quoted(w.targetServer) >> w.x >> w.y >> w.w >> w.h) loaded.push_back(w);
    if(!f.eof()) {std::cerr<<"[WARP] Invalid warps.db; config retained.\n";return;}
    warps_=std::move(loaded);
}
void WarpManager::console(const std::string& args) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::istringstream in(args);std::string action;in>>action;
    if(action.empty() || action=="list") {
        for(size_t i=0;i<warps_.size();++i) {auto& w=warps_[i];std::cout<<"[WARP] "<<i+1<<" "<<w.sourceServer<<" -> "<<w.targetServer<<" ["<<w.x<<","<<w.y<<","<<w.w<<","<<w.h<<"]\n";}
        return;
    }
    auto next=warps_;
    if(action=="add") {
        WarpPlate w;
        if(!(in>>std::quoted(w.sourceServer)>>std::quoted(w.targetServer)>>w.x>>w.y>>w.w>>w.h) ||
           !std::isfinite(w.x)||!std::isfinite(w.y)||!std::isfinite(w.w)||!std::isfinite(w.h)|| w.x<0 || w.y<0 || w.w<=0 || w.h<=0 ||
           !proxy_->findServer(w.sourceServer)||!proxy_->findServer(w.targetServer)) {
            std::cout<<"[WARP] warps add <source> <target> <x> <y> <width> <height> (tiles)\n";return;
        }
        w.sourceServer=proxy_->findServer(w.sourceServer)->name;w.targetServer=proxy_->findServer(w.targetServer)->name;
        next.push_back(w);
    } else if(action=="del") {
        size_t id=0;if(!(in>>id)||id==0||id>next.size()){std::cout<<"[WARP] Invalid ID.\n";return;}
        next.erase(next.begin()+id-1);
    } else {std::cout<<"[WARP] warps list, warps add <source> <target> <x> <y> <w> <h>, warps del <id>\n";return;}
    std::ofstream f("warps.db.tmp",std::ios::trunc);
    for(auto& w:next)f<<std::quoted(w.sourceServer)<<' '<<std::quoted(w.targetServer)<<' '<<w.x<<' '<<w.y<<' '<<w.w<<' '<<w.h<<'\n';
    f.close();if(!f){std::cerr<<"[WARP] Save failed.\n";return;}
#ifdef _WIN32
    if(!MoveFileExA("warps.db.tmp","warps.db",MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::cerr<<"[WARP] Replace failed.\n";return;}
#else
    std::error_code ec;std::filesystem::rename("warps.db.tmp","warps.db",ec);if(ec){std::cerr<<"[WARP] Replace failed.\n";return;}
#endif
    warps_=std::move(next);activeWarps_.clear();
    std::cout<<"[WARP] Saved warps.db\n";
}
