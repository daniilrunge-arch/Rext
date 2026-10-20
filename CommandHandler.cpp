#include "PingStyle.hpp"
#include "CommandHandler.hpp"
#include "ProxyServer.hpp"
#include "Session.hpp"
#include "NativeRtt.hpp"

#include <vector>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <mutex>
#include <ctime>

static std::string toLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---- last seen storage (shared with ProxyServer via touchLastSeen) ----
static std::mutex g_seenMtx;
static std::unordered_map<std::string, std::time_t> g_lastSeen;
static std::unordered_map<std::string, std::string> g_displayName;

void touchLastSeen(const std::string& name) {
    if (name.empty()) return;
    std::lock_guard<std::mutex> lk(g_seenMtx);
    std::string key = toLower(name);
    g_lastSeen[key] = std::time(nullptr);
    g_displayName[key] = name;
}

static std::string formatTime(std::time_t t) {
    if (t <= 0) return "никогда";
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &tm);
    return buf;
}

static std::string timeAgo(std::time_t t) {
    if (t <= 0) return "";
    auto now = std::time(nullptr);
    long long diff = static_cast<long long>(now - t);
    if (diff < 0) diff = 0;
    if (diff < 60) return std::to_string(diff) + " сек. назад";
    if (diff < 3600) return std::to_string(diff / 60) + " мин. назад";
    if (diff < 86400) return std::to_string(diff / 3600) + " ч. назад";
    return std::to_string(diff / 86400) + " дн. назад";
}

static void handlePing(ProxyServer& proxy, std::shared_ptr<Session> s) {
    const auto& cfg=proxy.getConfig();
    std::optional<double> client;
    if(cfg.tcpRttEnabled) {std::lock_guard<std::mutex> lock(s->clientWriteMtx);client=nativeRttMs(s->client);}
    if(!client){proxy.sendChat(s,cfg.phrase("tcpRttUnavailable"));return;}
    auto value=styledPing(*client,cfg.pingGreenMaxMs,cfg.pingYellowMaxMs);
    auto message=cfg.phrase("currentTcpRtt");
    for(auto token:{std::string("${overallRtt}"),std::string("${clientRtt}"),std::string("${rtt}")}){auto pos=message.find(token);if(pos!=message.npos)message.replace(pos,token.size(),value);}
    proxy.sendChat(s,message);

}

static bool isOnline(ProxyServer& proxy, const std::string& nameLower) {
    std::lock_guard<std::mutex> lk(proxy.getSessionsMutex());
    for (auto& kv : proxy.getSessions()) {
        auto& s = kv.second;
        if (!s->closed && !s->username.empty() && toLower(s->username) == nameLower)
            return true;
    }
    return false;
}

static void handlePlay(ProxyServer& proxy, std::shared_ptr<Session> s, const std::string& arg) {
    // refresh online players
    {
        std::lock_guard<std::mutex> lk(proxy.getSessionsMutex());
        for (auto& kv : proxy.getSessions()) {
            auto& sess = kv.second;
            if (!sess->closed && !sess->username.empty())
                touchLastSeen(sess->username);
        }
    }

    if (arg.empty()) {
        proxy.sendChat(s, "[c/00BFFF:/play] [c/FFFFFF:Использование: /play <ник>]", 255, 255, 255);
        proxy.sendChat(s, "[c/00BFFF:/play] [c/AAAAAA:Когда игрок был на сервере последний раз.]", 255, 255, 255);

        std::vector<std::pair<std::string, std::time_t>> list;
        {
            std::lock_guard<std::mutex> lk(g_seenMtx);
            for (auto& kv : g_lastSeen)
                list.push_back({kv.first, kv.second});
        }
        std::sort(list.begin(), list.end(),
            [](auto& a, auto& b) { return a.second > b.second; });

        int shown = 0;
        for (auto& p : list) {
            if (shown >= 12) break;
            std::string disp;
            {
                std::lock_guard<std::mutex> lk(g_seenMtx);
                auto it = g_displayName.find(p.first);
                disp = (it != g_displayName.end()) ? it->second : p.first;
            }
            if (isOnline(proxy, p.first)) {
                proxy.sendChat(s,
                    "[c/00FF00:●] [c/FFFFFF:" + disp + "] [c/00FF00:сейчас онлайн]",
                    255, 255, 255);
            } else {
                proxy.sendChat(s,
                    "[c/888888:○] [c/FFFFFF:" + disp + "] [c/AAAAAA:"
                    + formatTime(p.second) + " (" + timeAgo(p.second) + ")]",
                    255, 255, 255);
            }
            ++shown;
        }
        if (shown == 0)
            proxy.sendChat(s, "[c/FF5555:Пока нет данных. Игроки появятся после входа.]", 255, 255, 255);
        return;
    }

    std::string key = toLower(arg);
    std::time_t seen = 0;
    std::string disp = arg;
    {
        std::lock_guard<std::mutex> lk(g_seenMtx);
        auto it = g_lastSeen.find(key);
        if (it != g_lastSeen.end()) seen = it->second;
        auto it2 = g_displayName.find(key);
        if (it2 != g_displayName.end()) disp = it2->second;
    }

    if (isOnline(proxy, key)) {
        proxy.sendChat(s,
            "[c/00BFFF:/play] [c/FFFFFF:" + disp + "] [c/00FF00:сейчас на сервере!]",
            255, 255, 255);
        return;
    }

    if (seen <= 0) {
        proxy.sendChat(s,
            "[c/00BFFF:/play] [c/FFFFFF:" + arg + "] [c/FF5555:не найден в истории.]",
            255, 255, 255);
        proxy.sendChat(s,
            "[c/AAAAAA:Игрок ещё не заходил после запуска прокси.]",
            255, 255, 255);
        return;
    }

    proxy.sendChat(s, "[c/00BFFF:/play] [c/FFFFFF:" + disp + "]", 255, 255, 255);
    proxy.sendChat(s,
        "[c/AAAAAA:Последний раз был:] [c/FFFFFF:" + formatTime(seen) + "]"
        " [c/AAAAAA:(" + timeAgo(seen) + ")]",
        255, 255, 255);
}

bool CommandHandler::handle(ProxyServer& proxy, std::shared_ptr<Session> s, const std::string& textRaw) {
    std::string text = textRaw;
    if (!text.empty() && text[0] == '.') text[0] = '/';

    auto sp = text.find(' ');
    std::string cmd = toLower(sp == std::string::npos ? text : text.substr(0, sp));
    std::string args = sp == std::string::npos ? "" : text.substr(sp + 1);
    while (!args.empty() && args[0] == ' ') args.erase(0, 1);

    if(cmd=="/"+proxy.getConfig().phrase("dimensionsCommandName"))cmd="/dim";
    if (cmd.size()>1 && proxy.findServer(cmd.substr(1))) {
        args=cmd.substr(1); cmd="/join";
    }
    static const char* list[] = {
        "/join", "/dim", "/move", "/server",
        "/who", "/list", "/online", "/players",
        "/ping", "/servers", "/dims", "/commands", "/phelp",
        "/play", "/warp", "/myrules", "/rules", "/dimensions", "/void", "/back"
    };
    bool isProxy = false;
    for (auto* x : list) if (cmd == x) { isProxy = true; break; }
    if (!isProxy) return false;

    if (cmd == "/void") { proxy.enterVoid(s); return true; }
    if (cmd == "/back") {
        const auto* target=proxy.findServer(s->voidReturnServer);
        if (!target) target=proxy.defaultServer();
        if(target) {auto copy=*target;std::thread([&proxy,s,copy]{proxy.switchServer(s,copy);}).detach();}
        return true;
    }
    if (cmd == "/play") {
        handlePlay(proxy, s, args);
        return true;
    }

    if (cmd == "/ping") {
        handlePing(proxy, s);
        return !proxy.getConfig().pingPassThrough;
    }

    if (cmd == "/servers" || cmd == "/dims" || cmd == "/dimensions" || (cmd == "/dim" && args.empty())) {
        proxy.sendChat(s,proxy.getConfig().phrase("availableDimensions"),255,255,255);
        std::string names;
        for(const auto& server:proxy.getConfig().servers) {if(server.hidden)continue;if(!names.empty())names+=", ";names+=(toLower(server.name)==toLower(s->currentServer)?"[i:4085]":"[i:4083]")+std::string("[c/f88efa:/")+server.name+"]";}
        proxy.sendChat(s,"  "+names,255,255,255);
        return true;
    }

    if (cmd == "/myrules" || cmd == "/rules") {
        const auto grants = proxy.playerRules().list(s->username);
        proxy.sendChat(s, "[c/22D3EE:══════ ВАШИ ВРЕМЕННЫЕ ПРАВИЛА ══════]", 255, 255, 255);
        if (grants.empty()) {
            proxy.sendChat(s, "[c/AAAAAA:Активных временных правил нет.]", 255, 255, 255);
        } else {
            const auto now = static_cast<std::int64_t>(std::time(nullptr));
            for (const auto& rule : grants) {
                proxy.sendChat(s, "[c/FFFFFF:" + rule.rule + "] [c/AAAAAA:осталось " +
                    PlayerRuleManager::formatRemaining(rule.expiresAt - now) + "]", 255, 255, 255);
            }
        }
        return true;
    }

    if (cmd == "/warp") {
        if (args.empty()) {
            proxy.sendChat(s, "[c/22D3EE:Использование: /warp <сервер>]", 255, 255, 255);
            return true;
        }
        if (proxy.getConfig().requireWarpRule && !proxy.hasPlayerRule(s->username, "warp")) {
            proxy.sendChat(s, "[c/FF5555:Для /warp нужно временное правило warp. Обратитесь к администрации.]", 255, 255, 255);
            return true;
        }
        const auto* targetPtr = proxy.findServer(args);
        if (!targetPtr) {
            proxy.sendChat(s, "[c/FF5555:Сервер не найден. Используйте /servers.]", 255, 255, 255);
            return true;
        }
        if (toLower(s->currentServer) == toLower(targetPtr->name)) {
            proxy.sendChat(s, "[c/AAAAAA:Вы уже на сервере " + targetPtr->name + ".]", 255, 255, 255);
            return true;
        }
        const RoutingServer target = *targetPtr;
        std::thread([&proxy, s, target] { proxy.switchServer(s, target); }).detach();
        return true;
    }

    if (cmd == "/commands" || cmd == "/phelp") {
        proxy.sendChat(s, "[c/22D3EE:REXT-PROXY] [c/FFFFFF:/dim, /dimensions, /<server>, /join <server>, /void, /back, /warp <server>, /who, /ping, /play <ник>, /myrules]", 255, 255, 80);
        return true;
    }

    if (cmd == "/who" || cmd == "/list" || cmd == "/online" || cmd == "/players") {
        std::string current = s->currentServer.empty() ? "Lobby" : s->currentServer;
        std::vector<std::shared_ptr<Session>> all;
        {
            std::lock_guard<std::mutex> lk(proxy.getSessionsMutex());
            for (auto& kv : proxy.getSessions())
                if (!kv.second->closed && !kv.second->username.empty())
                    all.push_back(kv.second);
        }
        auto countText=proxy.getConfig().phrase("playerCount");
        auto marker=countText.find("${total}");if(marker!=std::string::npos)countText.replace(marker,8,std::to_string(all.size()));
        proxy.sendChat(s,countText,255,255,255);

        std::vector<std::string> here;
        for (auto& o : all)
            if (toLower(o->currentServer) == toLower(current))
                here.push_back(o->username);

        proxy.sendChat(s,
            "[c/8EFAD2:Игроки в вашем режиме (" + current + "):] [c/FFFFFF:"
                + std::to_string(here.size()) + "]", 255, 255, 255);
        if (!here.empty()) {
            std::string line;
            for (size_t i = 0; i < here.size(); ++i) {
                if (i) line += ", ";
                line += here[i];
            }
            proxy.sendChat(s, "[c/8EFAD2:Список:] [c/E0E0E0:" + line + "]", 255, 255, 255);
        } else {
            proxy.sendChat(s, "[c/FF5555:В вашем режиме сейчас никого нет.]", 255, 255, 255);
        }

        for (auto& srv : proxy.getConfig().servers) {
            if (toLower(srv.name) == toLower(current)) continue;
            std::vector<std::string> names;
            for (auto& o : all)
                if (toLower(o->currentServer) == toLower(srv.name))
                    names.push_back(o->username);
            if (!names.empty()) {
                std::string line;
                for (size_t i = 0; i < names.size(); ++i) {
                    if (i) line += ", ";
                    line += names[i];
                }
                proxy.sendChat(s, "[c/FFAA00:(" + srv.name + "):] [c/E0E0E0:" + line + "]",
                               255, 255, 255);
            }
        }
        return true;
    }

    if (cmd == "/join" || cmd == "/dim" || cmd == "/move" || cmd == "/server") {
        if (args.empty()) {
            proxy.sendChat(s,
                proxy.getConfig().phrase("specifyADimensionToTravel"),
                255, 255, 255);
            return true;
        }
        if (proxy.getConfig().requireJoinRule && !proxy.hasPlayerRule(s->username, "join")) {
            proxy.sendChat(s, "[c/FF5555:Для смены сервера нужно временное правило join.]", 255, 255, 255);
            return true;
        }
        auto* t = proxy.findServer(args);
        if (!t) {
            std::string names;
            for (size_t i = 0; i < proxy.getConfig().servers.size(); ++i) {
                if (i) names += ", ";
                names += proxy.getConfig().servers[i].name;
            }
            proxy.sendChat(s, "Неизвестное измерение. Доступные: " + names, 255, 80, 80);
            return true;
        }
        if (toLower(s->currentServer) == toLower(t->name)) {
            proxy.sendChat(s,
                proxy.getConfig().phrase("youAreAlreadyInthatDimension"),
                255, 255, 255);
            return true;
        }
        const RoutingServer target = *t;
        std::thread([&proxy, s, target] { proxy.switchServer(s, target); }).detach();
        return true;
    }
    return true;
}
