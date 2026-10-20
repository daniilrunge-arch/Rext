// PlayTime Extension — команда /play <ник>
// Показывает, когда игрок был на сервере в последний раз.
//
// Build with root build.bat/build.sh (host export/import library required).

#include "../../Extension.hpp"
#include "../../ProxyServer.hpp"
#include "../../Session.hpp"
#include "../../Packets.hpp"

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <vector>

#ifdef _WIN32
  #define EXPORT __declspec(dllexport)
#else
  #define EXPORT __attribute__((visibility("default")))
#endif

static std::string toLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
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

class PlayTimeExtension : public IExtension {
public:
    const char* name() const override { return "PlayTime"; }
    const char* version() const override { return "1.0.0"; }

    void onLoad(ProxyServer* proxy) override {
        proxy_ = proxy;
    }

    bool onClientPacket(std::shared_ptr<Session> session,
                        const std::vector<uint8_t>& frame) override {
        if (!session || frame.size() < 3) return false;

        uint8_t pkt = frame[2];

        // Обновляем last-seen при PlayerInfo (ник появился)
        if (pkt == static_cast<uint8_t>(PacketType::PlayerInfo)) {
            auto name = extractPlayerName(frame.data(), frame.size());
            if (name && !name->empty()) {
                touch(*name);
            }
            return false; // не блокируем
        }

        // Команда /play через чат (LoadNetModule)
        if (pkt != static_cast<uint8_t>(PacketType::LoadNetModule))
            return false;

        auto text = extractChatText(frame.data(), frame.size());
        if (!text) return false;

        std::string cmd = *text;
        if (!cmd.empty() && cmd[0] == '.') cmd[0] = '/';
        if (cmd.size() < 5) return false;

        // /play или /play <ник>
        std::string lower = toLower(cmd);
        if (lower.rfind("/play", 0) != 0) return false;
        // строго /play или /play ...
        if (lower.size() > 5 && lower[5] != ' ') return false;

        std::string arg;
        if (cmd.size() > 5) {
            arg = cmd.substr(5);
            while (!arg.empty() && arg[0] == ' ') arg.erase(0, 1);
        }

        handlePlay(session, arg);
        return true; // съели пакет — на бэкенд не уходит
    }

    bool onServerPacket(std::shared_ptr<Session> /*session*/,
                        const std::vector<uint8_t>& /*frame*/) override {
        return false;
    }

private:
    ProxyServer* proxy_ = nullptr;
    std::mutex mtx_;
    // lowercase name -> last seen unix time
    std::unordered_map<std::string, std::time_t> lastSeen_;
    // lowercase name -> original display name
    std::unordered_map<std::string, std::string> displayName_;

    void touch(const std::string& name) {
        std::lock_guard<std::mutex> lk(mtx_);
        std::string key = toLower(name);
        lastSeen_[key] = std::time(nullptr);
        displayName_[key] = name;
    }

    bool isOnline(const std::string& nameLower) const {
        if (!proxy_) return false;
        std::lock_guard<std::mutex> lk(const_cast<ProxyServer*>(proxy_)->getSessionsMutex());
        for (auto& kv : const_cast<ProxyServer*>(proxy_)->getSessions()) {
            auto& s = kv.second;
            if (!s->closed && !s->username.empty() && toLower(s->username) == nameLower)
                return true;
        }
        return false;
    }

    void handlePlay(std::shared_ptr<Session> session, const std::string& arg) {
        if (!proxy_) return;

        // Без аргумента — список недавних
        if (arg.empty()) {
            proxy_->sendChat(session,
                "[c/00BFFF:/play] [c/FFFFFF:Использование: /play <ник>]",
                255, 255, 255);
            proxy_->sendChat(session,
                "[c/00BFFF:/play] [c/AAAAAA:Показывает, когда игрок был на сервере последний раз.]",
                255, 255, 255);

            // Топ-10 последних
            std::vector<std::pair<std::string, std::time_t>> list;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                for (auto& kv : lastSeen_)
                    list.push_back({kv.first, kv.second});
            }
            std::sort(list.begin(), list.end(),
                [](auto& a, auto& b) { return a.second > b.second; });

            int shown = 0;
            for (auto& p : list) {
                if (shown >= 10) break;
                std::string disp;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    auto it = displayName_.find(p.first);
                    disp = (it != displayName_.end()) ? it->second : p.first;
                }
                bool online = isOnline(p.first);
                if (online) {
                    proxy_->sendChat(session,
                        "[c/00FF00:●] [c/FFFFFF:" + disp + "] [c/00FF00:сейчас онлайн]",
                        255, 255, 255);
                } else {
                    proxy_->sendChat(session,
                        "[c/888888:○] [c/FFFFFF:" + disp + "] [c/AAAAAA:"
                        + formatTime(p.second) + " (" + timeAgo(p.second) + ")]",
                        255, 255, 255);
                }
                ++shown;
            }
            if (shown == 0) {
                proxy_->sendChat(session,
                    "[c/FF5555:Пока нет данных о игроках.]",
                    255, 255, 255);
            }
            return;
        }

        // /play <ник>
        std::string key = toLower(arg);
        // Обновим last-seen для всех онлайн (на всякий случай)
        if (proxy_) {
            std::lock_guard<std::mutex> lk(proxy_->getSessionsMutex());
            for (auto& kv : proxy_->getSessions()) {
                auto& s = kv.second;
                if (!s->closed && !s->username.empty()) {
                    std::lock_guard<std::mutex> lk2(mtx_);
                    std::string k = toLower(s->username);
                    lastSeen_[k] = std::time(nullptr);
                    displayName_[k] = s->username;
                }
            }
        }

        std::time_t seen = 0;
        std::string disp = arg;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = lastSeen_.find(key);
            if (it != lastSeen_.end()) seen = it->second;
            auto it2 = displayName_.find(key);
            if (it2 != displayName_.end()) disp = it2->second;
        }

        if (isOnline(key)) {
            proxy_->sendChat(session,
                "[c/00BFFF:/play] [c/FFFFFF:" + disp + "] [c/00FF00:сейчас на сервере!]",
                255, 255, 255);
            return;
        }

        if (seen <= 0) {
            proxy_->sendChat(session,
                "[c/00BFFF:/play] [c/FFFFFF:" + arg + "] [c/FF5555:не найден в истории.]",
                255, 255, 255);
            proxy_->sendChat(session,
                "[c/AAAAAA:Игрок ещё не заходил после запуска прокси.]",
                255, 255, 255);
            return;
        }

        proxy_->sendChat(session,
            "[c/00BFFF:/play] [c/FFFFFF:" + disp + "]",
            255, 255, 255);
        proxy_->sendChat(session,
            "[c/AAAAAA:Последний раз был:] [c/FFFFFF:" + formatTime(seen) + "]"
            " [c/AAAAAA:(" + timeAgo(seen) + ")]",
            255, 255, 255);
    }
};

// ---- C ABI exports ----
extern "C" {

EXPORT int get_extension_api() { return REXT_EXTENSION_API; }

EXPORT IExtension* create_extension() {
    return new PlayTimeExtension();
}

EXPORT void destroy_extension(IExtension* e) {
    delete e;
}

EXPORT ExtensionInfo get_extension_info() {
    return { "PlayTime", "1.0.0" };
}

} // extern "C"
