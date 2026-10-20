#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct PlayerRuleGrant {
    std::string player;
    std::string rule;
    std::int64_t expiresAt = 0; // Unix epoch seconds
};

// Persistent, per-nickname temporary rule grants used by the proxy and warp manager.
class PlayerRuleManager {
public:
    explicit PlayerRuleManager(std::string path = "player_rules.db");

    static std::string normalize(std::string value);
    static std::optional<std::int64_t> parseDuration(const std::string& raw);
    static std::string formatRemaining(std::int64_t seconds);

    bool grant(const std::string& player, const std::string& rule, std::int64_t durationSeconds);
    bool revoke(const std::string& player, const std::string& rule);
    bool has(const std::string& player, const std::string& rule);
    std::vector<PlayerRuleGrant> list(const std::string& playerFilter = "");
    std::string path() const;

private:
    std::string path_;
    std::mutex mutex_;
    std::vector<PlayerRuleGrant> grants_;

    static bool validName(const std::string& value);
    static bool validRule(const std::string& value);
    void load();
    bool removeExpiredUnlocked();
    void saveUnlocked();
};
