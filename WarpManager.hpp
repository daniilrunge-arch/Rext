#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <cstdint>
#include <unordered_set>

#include "Session.hpp"
#include "Config.hpp"

class ProxyServer;

struct WarpPlate {
    float x = 0, y = 0, w = 0, h = 0;
    std::string targetServer;
    std::string sourceServer = "Lobby";

    bool contains(float px, float py) const {
        float tx = px / 16.0f;
        float ty = py / 16.0f;
        return (x <= tx && tx <= x + w) && (y <= ty && ty <= y + h);
    }
};

class WarpManager {
public:
    explicit WarpManager(ProxyServer* proxy);

    void setWarps(const std::vector<WarpDef>& defs);
    void console(const std::string& args);
    void loadSaved();
    void handleUpdatePlayer(std::shared_ptr<Session> session, const uint8_t* frame, size_t size);

private:
    ProxyServer* proxy_;
    std::unordered_map<int, double> cooldowns_;
    // One denial per visit to any warp plate; cleared once the player leaves the plates.
    std::unordered_set<int> deniedPlayers_;
    std::mutex mutex_;

    struct ActiveWarp {
        WarpPlate warp;
        bool cancelled = false;
    };
    std::unordered_map<int, ActiveWarp> activeWarps_;
    std::vector<WarpPlate> warps_;

    void doWarpCountdown(std::shared_ptr<Session> session, WarpPlate warp);
};
