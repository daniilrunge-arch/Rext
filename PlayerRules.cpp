#include "PlayerRules.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

PlayerRuleManager::PlayerRuleManager(std::string path) : path_(std::move(path)) {
    load();
}

std::string PlayerRuleManager::normalize(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::optional<std::int64_t> PlayerRuleManager::parseDuration(const std::string& raw) {
    if (raw.size() < 2) return std::nullopt;
    const std::string number = raw.substr(0, raw.size() - 1);
    const char unit = static_cast<char>(std::tolower(static_cast<unsigned char>(raw.back())));
    std::int64_t n = 0;
    try {
        std::size_t used = 0;
        n = std::stoll(number, &used);
        if (used != number.size() || n <= 0) return std::nullopt;
    } catch (...) { return std::nullopt; }
    std::int64_t factor = 0;
    switch (unit) {
        case 's': factor = 1; break;
        case 'm': factor = 60; break;
        case 'h': factor = 3600; break;
        case 'd': factor = 86400; break;
        case 'w': factor = 604800; break;
        default: return std::nullopt;
    }
    if (n > (365LL * 86400LL) / factor) return std::nullopt;
    return n * factor;
}

std::string PlayerRuleManager::formatRemaining(std::int64_t seconds) {
    if (seconds <= 0) return "истекло";
    const auto days = seconds / 86400; seconds %= 86400;
    const auto hours = seconds / 3600; seconds %= 3600;
    const auto minutes = seconds / 60; seconds %= 60;
    std::ostringstream out;
    if (days) out << days << "д ";
    if (hours) out << hours << "ч ";
    if (minutes) out << minutes << "м ";
    if (!days && !hours && !minutes) out << seconds << "с";
    std::string value = out.str();
    while (!value.empty() && value.back() == ' ') value.pop_back();
    return value;
}

bool PlayerRuleManager::grant(const std::string& player, const std::string& rule, std::int64_t durationSeconds) {
    if (!validName(player) || !validRule(rule) || durationSeconds <= 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    removeExpiredUnlocked();
    const std::string p = normalize(player), r = normalize(rule);
    const std::int64_t expiry = static_cast<std::int64_t>(std::time(nullptr)) + durationSeconds;
    auto it = std::find_if(grants_.begin(), grants_.end(), [&](const PlayerRuleGrant& item) {
        return normalize(item.player) == p && normalize(item.rule) == r;
    });
    if (it == grants_.end()) grants_.push_back(PlayerRuleGrant{player, r, expiry});
    else { it->player = player; it->rule = r; it->expiresAt = expiry; }
    saveUnlocked();
    return true;
}

bool PlayerRuleManager::revoke(const std::string& player, const std::string& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string p = normalize(player), r = normalize(rule);
    const auto before = grants_.size();
    grants_.erase(std::remove_if(grants_.begin(), grants_.end(), [&](const PlayerRuleGrant& item) {
        return normalize(item.player) == p && normalize(item.rule) == r;
    }), grants_.end());
    const bool removed = grants_.size() != before;
    if (removed) saveUnlocked();
    return removed;
}

bool PlayerRuleManager::has(const std::string& player, const std::string& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (removeExpiredUnlocked()) saveUnlocked();
    const std::string p = normalize(player), r = normalize(rule);
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    return std::any_of(grants_.begin(), grants_.end(), [&](const PlayerRuleGrant& item) {
        return normalize(item.player) == p &&
               (normalize(item.rule) == r || normalize(item.rule) == "all") && item.expiresAt > now;
    });
}

std::vector<PlayerRuleGrant> PlayerRuleManager::list(const std::string& playerFilter) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (removeExpiredUnlocked()) saveUnlocked();
    std::vector<PlayerRuleGrant> out;
    const std::string filter = normalize(playerFilter);
    for (const auto& item : grants_)
        if (filter.empty() || normalize(item.player) == filter) out.push_back(item);
    return out;
}

std::string PlayerRuleManager::path() const { return path_; }

bool PlayerRuleManager::validName(const std::string& value) {
    if (value.empty() || value.size() > 20) return false;
    for (unsigned char c : value)
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}

bool PlayerRuleManager::validRule(const std::string& value) {
    if (value.empty() || value.size() > 48) return false;
    for (unsigned char c : value)
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == ':')) return false;
    return true;
}

void PlayerRuleManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream in(path_);
    if (!in) return;
    std::string player, rule;
    std::int64_t expiry = 0;
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    while (in >> std::quoted(player) >> std::quoted(rule) >> expiry) {
        if (validName(player) && validRule(rule) && expiry > now)
            grants_.push_back(PlayerRuleGrant{player, normalize(rule), expiry});
    }
    saveUnlocked();
}

bool PlayerRuleManager::removeExpiredUnlocked() {
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    const auto before = grants_.size();
    grants_.erase(std::remove_if(grants_.begin(), grants_.end(), [now](const PlayerRuleGrant& item) {
        return item.expiresAt <= now;
    }), grants_.end());
    return before != grants_.size();
}

void PlayerRuleManager::saveUnlocked() {
    std::error_code ec;
    const std::filesystem::path target(path_);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    for (const auto& item : grants_)
        out << std::quoted(item.player) << ' ' << std::quoted(item.rule) << ' ' << item.expiresAt << '\n';
}
