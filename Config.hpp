#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include "Yaml.hpp"
#include "Language.hpp"

struct RoutingServer {
    std::string name, host;
    int port = 0;
    bool hidden = false;
    bool isVanilla = true;
};

struct WarpDef {
    float x = 0, y = 0, w = 0, h = 0;
    std::string target;
    std::string source = "Lobby";
};

struct ListenerConfig {int port=7777;std::vector<std::string> pool;};

struct Config {
    std::vector<ListenerConfig> listeners;
    bool restEnabled=true, socketNoDelay=true, tcpRttEnabled=true, exportRtt=false, pingPassThrough=false, blockInvis=false;
    int socketTimeoutMs=0, sampleIntervalMs=2000, connectionLimitPerIP=0, connectionRateLimitPerIP=0, rateWindowSeconds=10;
    int pingGreenMaxMs=80,pingYellowMaxMs=150;
    bool logConsole=true,logFile=true;
    std::string logConsoleFormat="PlainText",logFileFormat="PlainText";
    int restServerPort=0;
    bool restHasPassword=false;
    std::string nameChangeMode="legacy";
    std::vector<std::string> nameChangeExclusions;
    std::string connectionLimitReason;
    std::string kickPolicy="never";
    std::vector<std::string> kickPrefixes;
    std::map<std::string,std::string> phrases=languageDefaults(false);
    std::string phrase(const std::string& key)const{auto it=phrases.find(key);return it==phrases.end()?key:it->second;}
    int listenPort = 7777;
    std::string terrariaVersion = "Terraria326";
    int maxPlayers = 100;
    int connectTimeoutSec = 6;
    std::vector<RoutingServer> servers;

    std::string serverOfflineMessage = "Измерение недоступно. Попробуйте позже.";
    std::string kickMessage = "Измерение, в котором вы были, потеряло связь.";
    std::string switchMessage = "[i:4076][c/d62ef0:Измерения] [c/8efad2:Перемещаем вас в новое измерение] {server}...";
    std::string loadingStatusMessage = "Подключение к измерению {server}...";
    std::string kickbackMessage = "Связь с измерением потеряна. Возвращаем в Lobby...";
    std::string dimensionDroppedMessage = "Измерение, в котором вы были, потеряло связь.";

    std::string logDirectory = "logs";
    bool packetLogging = true;
    int logMaxMB = 8;
    int packetLogMaxMB = 16;
    int logBackups = 5;
    int packetHexBytes = 48;

    int restPort = 9999;
    std::string restName = "you server";
    std::string restWorld = "Dimensions";
    int restMaxPlayers = 400;
    std::string restVersion = "v1.4.5.8";

    std::vector<WarpDef> warps;

    bool requireWarpRule = false; // warp plates and /warp require a temporary "warp" rule
    bool requireJoinRule = false; // keep direct /join compatible unless explicitly enabled

    // Built-in security modules. Configurable so owners can tune them without rebuilding.
    bool antiNickEnabled = true;
    int minNickLength = 2;
    int maxNickLength = 20;
    std::vector<std::string> reservedNicknames{
        "admin", "administrator", "moderator", "mod", "owner", "console", "server", "rextproxy"
    };
    bool antiSpamEnabled = true;
    int maxChatMessagesPer10Seconds = 8;
    int duplicateChatCooldownMs = 1500;
    bool enforceMaxPlayers = true;

    bool debuffOnSwitch = true;
    std::vector<int> debuffIds{163, 47, 149}; // Webbed + Chilled
    int debuffSeconds = 10;
};

namespace cfg_util {
inline std::string trim(std::string s) {
    auto ns = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
    s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
    return s;
}
inline bool findStr(const std::string& j, const std::string& key, std::string& out) {
    auto p = j.find("\"" + key + "\"");
    if (p == std::string::npos) return false;
    p = j.find(':', p);
    if (p == std::string::npos) return false;
    auto a = j.find_first_not_of(" \t\r\n", p + 1);
    if (a == std::string::npos) return false;
    if (j[a] == '"') {
        auto b = j.find('"', a + 1);
        if (b == std::string::npos) return false;
        out = j.substr(a + 1, b - a - 1);
        return true;
    }
    auto b = j.find_first_of(",}\r\n]", a);
    out = trim(j.substr(a, b == std::string::npos ? std::string::npos : b - a));
    return true;
}
inline bool findInt(const std::string& j, const std::string& key, int& out) {
    std::string s;
    if (!findStr(j, key, s)) return false;
    try { out = std::stoi(s); return true; } catch (...) { return false; }
}
inline bool findBool(const std::string& j, const std::string& key, bool& out) {
    std::string s;
    if (!findStr(j, key, s)) return false;
    out = (s == "true" || s == "1");
    return true;
}
inline bool findFloat(const std::string& j, const std::string& key, float& out) {
    std::string s;
    if (!findStr(j, key, s)) return false;
    try { out = std::stof(s); return true; } catch (...) { return false; }
}
inline bool findStringArray(const std::string& j, const std::string& key, std::vector<std::string>& out) {
    auto p = j.find("\"" + key + "\"");
    if (p == std::string::npos) return false;
    p = j.find('[', p);
    if (p == std::string::npos) return false;
    const auto end = j.find(']', p);
    if (end == std::string::npos) return false;
    std::vector<std::string> values;
    std::size_t i = p + 1;
    while (i < end) {
        i = j.find('\"', i);
        if (i == std::string::npos || i >= end) break;
        const auto close = j.find('\"', i + 1);
        if (close == std::string::npos || close > end) break;
        values.push_back(j.substr(i + 1, close - i - 1));
        i = close + 1;
    }
    out = std::move(values);
    return true;
}
}

inline bool loadJsonConfig(const std::string& path, Config& cfg) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Cannot open " << path << "\n"; return false; }
    std::ostringstream ss; ss << in.rdbuf();
    std::string j = ss.str();

    cfg_util::findInt(j, "listenPort", cfg.listenPort);
    cfg_util::findInt(j, "maxPlayers", cfg.maxPlayers);
    cfg_util::findInt(j, "connectTimeoutSec", cfg.connectTimeoutSec);
    cfg_util::findInt(j, "restPort", cfg.restPort);
    cfg_util::findInt(j, "restMaxPlayers", cfg.restMaxPlayers);
    cfg_util::findInt(j, "debuffSeconds", cfg.debuffSeconds);
    cfg_util::findBool(j, "packetLogging", cfg.packetLogging);
    cfg_util::findInt(j, "logMaxMB", cfg.logMaxMB);
    cfg_util::findInt(j, "packetLogMaxMB", cfg.packetLogMaxMB);
    cfg_util::findInt(j, "logBackups", cfg.logBackups);
    cfg_util::findInt(j, "packetHexBytes", cfg.packetHexBytes);
    cfg_util::findBool(j, "debuffOnSwitch", cfg.debuffOnSwitch);
    cfg_util::findBool(j, "requireWarpRule", cfg.requireWarpRule);
    cfg_util::findBool(j, "requireJoinRule", cfg.requireJoinRule);
    cfg_util::findBool(j, "antiNickEnabled", cfg.antiNickEnabled);
    cfg_util::findInt(j, "minNickLength", cfg.minNickLength);
    cfg_util::findInt(j, "maxNickLength", cfg.maxNickLength);
    cfg_util::findStringArray(j, "reservedNicknames", cfg.reservedNicknames);
    cfg_util::findBool(j, "antiSpamEnabled", cfg.antiSpamEnabled);
    cfg_util::findInt(j, "maxChatMessagesPer10Seconds", cfg.maxChatMessagesPer10Seconds);
    cfg_util::findInt(j, "duplicateChatCooldownMs", cfg.duplicateChatCooldownMs);
    cfg_util::findBool(j, "enforceMaxPlayers", cfg.enforceMaxPlayers);
    cfg.minNickLength = std::clamp(cfg.minNickLength, 1, 20);
    cfg.maxNickLength = std::clamp(cfg.maxNickLength, cfg.minNickLength, 32);
    cfg.maxChatMessagesPer10Seconds = std::clamp(cfg.maxChatMessagesPer10Seconds, 2, 100);
    cfg.duplicateChatCooldownMs = std::clamp(cfg.duplicateChatCooldownMs, 0, 60000);
    std::string t;
    if (cfg_util::findStr(j, "terrariaVersion", t)) cfg.terrariaVersion = t;
    if (cfg_util::findStr(j, "logDirectory", t)) cfg.logDirectory = t;
    if (cfg_util::findStr(j, "serverOfflineMessage", t)) cfg.serverOfflineMessage = t;
    if (cfg_util::findStr(j, "kickMessage", t)) cfg.kickMessage = t;
    if (cfg_util::findStr(j, "switchMessage", t)) cfg.switchMessage = t;
    if (cfg_util::findStr(j, "loadingStatusMessage", t)) cfg.loadingStatusMessage = t;
    if (cfg_util::findStr(j, "kickbackMessage", t)) cfg.kickbackMessage = t;
    if (cfg_util::findStr(j, "dimensionDroppedMessage", t)) cfg.dimensionDroppedMessage = t;
    if (cfg_util::findStr(j, "restName", t)) cfg.restName = t;
    if (cfg_util::findStr(j, "restWorld", t)) cfg.restWorld = t;
    if (cfg_util::findStr(j, "restVersion", t)) cfg.restVersion = t;

    auto pos = j.find("\"servers\"");
    if (pos != std::string::npos) {
        pos = j.find('[', pos);
        auto end = j.find(']', pos);
        if (pos != std::string::npos && end != std::string::npos) {
            std::string arr = j.substr(pos + 1, end - pos - 1);
            size_t i = 0;
            while (true) {
                auto o1 = arr.find('{', i);
                if (o1 == std::string::npos) break;
                auto o2 = arr.find('}', o1);
                if (o2 == std::string::npos) break;
                std::string obj = arr.substr(o1, o2 - o1 + 1);
                RoutingServer s;
                cfg_util::findStr(obj, "name", s.name);
                cfg_util::findStr(obj, "host", s.host);
                cfg_util::findInt(obj, "port", s.port);
                if (!s.name.empty() && s.port > 0) cfg.servers.push_back(s);
                i = o2 + 1;
            }
        }
    }

    pos = j.find("\"warps\"");
    if (pos != std::string::npos) {
        pos = j.find('[', pos);
        auto end = j.find(']', pos);
        if (pos != std::string::npos && end != std::string::npos) {
            std::string arr = j.substr(pos + 1, end - pos - 1);
            size_t i = 0;
            while (true) {
                auto o1 = arr.find('{', i);
                if (o1 == std::string::npos) break;
                auto o2 = arr.find('}', o1);
                if (o2 == std::string::npos) break;
                std::string obj = arr.substr(o1, o2 - o1 + 1);
                WarpDef w;
                cfg_util::findFloat(obj, "x", w.x);
                cfg_util::findFloat(obj, "y", w.y);
                cfg_util::findFloat(obj, "w", w.w);
                cfg_util::findFloat(obj, "h", w.h);
                cfg_util::findStr(obj, "target", w.target);
                cfg_util::findStr(obj, "source", w.source);
                if (!w.target.empty()) cfg.warps.push_back(w);
                i = o2 + 1;
            }
        }
    }

    pos = j.find("\"debuffIds\"");
    if (pos != std::string::npos) {
        pos = j.find('[', pos);
        auto end = j.find(']', pos);
        if (pos != std::string::npos && end != std::string::npos) {
            cfg.debuffIds.clear();
            std::string arr = j.substr(pos + 1, end - pos - 1);
            std::stringstream ss2(arr);
            std::string part;
            while (std::getline(ss2, part, ',')) {
                try { cfg.debuffIds.push_back(std::stoi(cfg_util::trim(part))); } catch (...) {}
            }
        }
    }
    return !cfg.servers.empty();
}

inline bool loadYamlConfig(const std::string& path,Config& cfg) {
 try {
 auto root=yaml::Parser().read(path);cfg.servers.clear();cfg.listeners.clear();std::set<std::string> names;std::set<int> ports;
 for(auto& group:root["servers"].list){ListenerConfig l;l.port=group["listenPort"].integer(0);
  for(auto& n:group["routingServers"].list){RoutingServer t;t.name=n["name"].str();t.host=n["serverIP"].str();t.port=n["serverPort"].integer(0);t.hidden=n["hidden"].boolean(false);t.isVanilla=n["isVanilla"].boolean(false);
   std::string lower=t.name;for(auto& c:lower)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   if(lower.empty()||!names.insert(lower).second||t.host.empty()||t.port<1||t.port>65535)throw std::runtime_error("Invalid or duplicate routing server");cfg.servers.push_back(t);l.pool.push_back(t.name);
  }
  if(l.port){if(l.port<1||l.port>65535||l.pool.empty()||!ports.insert(l.port).second)throw std::runtime_error("Invalid listener/pool");cfg.listeners.push_back(l);}
 }
 if(cfg.listeners.empty()||cfg.servers.empty())throw std::runtime_error("At least one listener and backend required");cfg.listenPort=cfg.listeners[0].port;
 auto& o=root["options"];auto lang=o["language"].str("russian");if(lang!="ru"&&lang!="russian"&&lang!="en"&&lang!="english")throw std::runtime_error("language must be russian/ru or english/en");cfg.phrases=languageDefaults(lang=="en"||lang=="english");
 for(auto& e:o["languageOverrides"].map)cfg.phrases[e.first]=e.second.value;
 cfg.socketTimeoutMs=o["socketTimeout"].integer(15000);cfg.socketNoDelay=o["socketNoDelay"].boolean(true);
 cfg.restEnabled=o["restApi"]["enabled"].boolean(true);cfg.restPort=o["restApi"]["port"].integer(9999);
 auto& response=o["restApi"]["response"];cfg.restName=response["name"].str(cfg.restName);cfg.restWorld=response["worldName"].str(cfg.restWorld);cfg.restMaxPlayers=response["maxPlayers"].integer(cfg.restMaxPlayers);cfg.restVersion=response["version"].str(cfg.restVersion);
 cfg.restServerPort=response["terrariaServerPort"].integer(cfg.listenPort);cfg.restHasPassword=response["hasServerPassword"].boolean(false);
 cfg.logConsole=o["log"]["outputToConsole"].boolean(true);cfg.logFile=o["log"]["outputToFile"].boolean(true);
 cfg.logConsoleFormat=o["log"]["format"]["console"].str("PlainText");cfg.logFileFormat=o["log"]["format"]["file"].str("PlainText");
 for(auto format:{cfg.logConsoleFormat,cfg.logFileFormat})if(format!="PlainText"&&format!="JSON")throw std::runtime_error("log format must be PlainText or JSON");
 auto& rtt=o["tcpRtt"];cfg.tcpRttEnabled=rtt["enabled"].boolean(true);cfg.exportRtt=rtt["exportToServers"].boolean(false);cfg.sampleIntervalMs=rtt["sampleIntervalMs"].integer(2000);cfg.pingPassThrough=rtt["pingCommandPassThrough"].boolean(false);
 cfg.nameChangeMode=o["nameChanges"]["mode"].str("rewrite");
 if(cfg.nameChangeMode!="legacy"&&cfg.nameChangeMode!="rewrite")throw std::runtime_error("nameChanges.mode must be legacy or rewrite");
 for(auto& e:o["nameChanges"]["exclusions"].list)cfg.nameChangeExclusions.push_back(e.value);
 cfg.connectionLimitReason=o["connectionLimit"]["kickReason"].str(cfg.phrase("connectionRateLimitExceeded"));
 cfg.pingGreenMaxMs=o["tcpRtt"]["greenMaxMs"].integer(80);cfg.pingYellowMaxMs=o["tcpRtt"]["yellowMaxMs"].integer(150);
 if(cfg.pingGreenMaxMs<0||cfg.pingYellowMaxMs<cfg.pingGreenMaxMs)throw std::runtime_error("Invalid ping color thresholds");
 cfg.blockInvis=o["blockInvis"].boolean(false);
 cfg.antiSpamEnabled=o["rext"]["antiSpamEnabled"].boolean(true);
 cfg.packetLogging=o["rext"]["packetLogging"].boolean(false);
 cfg.connectTimeoutSec=o["rext"]["connectTimeoutSeconds"].integer(3);
 if(cfg.connectTimeoutSec<1||cfg.connectTimeoutSec>60)throw std::runtime_error("Invalid connect timeout");
 auto& d=o["debuffOnSwitch"];cfg.debuffOnSwitch=d["enabled"].boolean(true);cfg.debuffSeconds=d["debuffTimeInSeconds"].integer(5);
 if(d.map.count("buffTypes")){cfg.debuffIds.clear();for(auto& b:d["buffTypes"].list){int id=b.integer(0);if(id<1||id>65535)throw std::runtime_error("Invalid buff ID");cfg.debuffIds.push_back(id);}}
 cfg.kickPolicy=o["disconnectOnKick"]["type"].str("never");if(cfg.kickPolicy!="never"&&cfg.kickPolicy!="always"&&cfg.kickPolicy!="onKickReasonPrefix")throw std::runtime_error("Invalid disconnectOnKick.type");for(auto& x:o["disconnectOnKick"]["kickReasonPrefixes"].list)cfg.kickPrefixes.push_back(x.value);
 if(o["connectionLimit"]["enabled"].boolean(false))cfg.connectionLimitPerIP=o["connectionLimit"]["connectionLimitPerIP"].integer(2);
 if(o["connectionRateLimit"]["enabled"].boolean(false))cfg.connectionRateLimitPerIP=o["connectionRateLimit"]["connectionRateLimitPerIP"].integer(2);
 cfg.rateWindowSeconds=o["connectionRateLimit"]["connectionRateLimitWindowSeconds"].integer(10);
 if(o["fakeVersion"]["enabled"].boolean(false))cfg.terrariaVersion="Terraria"+std::to_string(o["fakeVersion"]["terrariaVersion"].integer(326));
 if(cfg.debuffSeconds<0||cfg.debuffSeconds>3600||cfg.sampleIntervalMs<100||cfg.socketTimeoutMs<0||cfg.rateWindowSeconds<1||cfg.connectionLimitPerIP<0||cfg.connectionRateLimitPerIP<0)throw std::runtime_error("Invalid option range");
 if(o["blacklist"]["enabled"].boolean(false)||o["redis"]["enabled"].boolean(false))throw std::runtime_error("Remote blacklist/Redis need an external integration; disable them in this build");
 if(o["hotReload"].boolean(false))std::cout<<"[WARN] [CONFIG] hotReload is not supported; restart to apply changes.\n";


 for(const auto& entry:o["log"].map) {
  if(entry.first!="format" && entry.first!="outputToConsole" && entry.first!="outputToFile") {
   std::cout<<"[WARN] [CONFIG] Per-event JS log filters are not implemented; format/output settings are applied.\n";break;
  }
 }
 if(rtt["restApiEndpoint"].boolean(false))std::cout<<"[WARN] [CONFIG] tcpRtt.restApiEndpoint is not implemented. /ping is configured separately.\n";
 cfg.serverOfflineMessage=cfg.phrase("noServersAvailable");cfg.dimensionDroppedMessage=cfg.phrase("dimensionDropped");cfg.switchMessage=cfg.phrase("shiftingToDimension");cfg.loadingStatusMessage="";
 return true;
 } catch(const std::exception& e){std::cerr<<"[CONFIG] "<<e.what()<<"\n";return false;}
}
inline bool loadConfig(const std::string& path,Config& cfg){auto dot=path.find_last_of('.');auto ext=dot==path.npos?"":path.substr(dot);return ext==".yaml"||ext==".yml"?loadYamlConfig(path,cfg):loadJsonConfig(path,cfg);}
