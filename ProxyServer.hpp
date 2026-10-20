#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Config.hpp"
#include "Session.hpp"
#include "Framer.hpp"
#include "Packets.hpp"
#include "ExtensionManager.hpp"
#include "WarpManager.hpp"
#include "RestApi.hpp"
#include "CommandHandler.hpp"
#include "PlayerRules.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
#include "BanStore.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>
#include <thread>

class ProxyServer {
public:
    explicit ProxyServer(Config cfg);
    ~ProxyServer();

    void start();
    void stop();
    void sendRealIp(const std::shared_ptr<Session>& s, socket_t socket, std::uint64_t generation);

    bool sendAll(socket_t sock, const uint8_t* data, size_t n);
    bool sendAll(socket_t sock, const std::vector<uint8_t>& data) {
        return sendAll(sock, data.data(), data.size());
    }
    // Session-level writers serialize whole frames so concurrent chat/world writes
    // cannot interleave bytes and corrupt Terraria's length-prefixed packet stream.
    bool sendToClient(const std::shared_ptr<Session>& s, const std::vector<uint8_t>& data,
                      std::uint64_t expectedGeneration = 0, bool allowClosed = false);
    bool sendToBackend(const std::shared_ptr<Session>& s, const std::vector<uint8_t>& data);
    bool sendToBackend(const std::shared_ptr<Session>& s, socket_t expectedSocket,
                       std::uint64_t expectedGeneration, const std::vector<uint8_t>& data);

    const RoutingServer* findServer(const std::string& name) const;
    const RoutingServer* defaultServer() const;
    const RoutingServer* lobbyServer() const;

    void sendChat(std::shared_ptr<Session> s, const std::string& text,
                  uint8_t r = 255, uint8_t g = 255, uint8_t b = 100);
    void disconnectSession(std::shared_ptr<Session> s, const std::string& reason);
    void enterVoid(std::shared_ptr<Session> s);
    void switchServer(std::shared_ptr<Session> s, const RoutingServer& target);

    const Config& getConfig() const { return cfg_; }
    bool hasPlayerRule(const std::string& player, const std::string& rule) { return playerRules_.has(player, rule); }
    PlayerRuleManager& playerRules() { return playerRules_; }
    std::mutex& getSessionsMutex() { return sessionsMtx_; }
    std::unordered_map<int, std::shared_ptr<Session>>& getSessions() { return sessions_; }

private:
    std::thread consoleThread_;
    std::atomic<unsigned> workers_{0};
    std::mutex stopMutex_;
    std::shared_ptr<std::atomic<bool>> consoleAlive_=std::make_shared<std::atomic<bool>>(true);
    Config cfg_;
    BanStore bans_;
    std::vector<socket_t> extraListeners_;
    std::vector<std::thread> listenerThreads_;
    std::mutex admissionMutex_;
    std::map<std::string,std::vector<int64_t>> connectionTimes_;
    socket_t listenSock_ = INVALID_SOCK;
    std::mutex sessionsMtx_;
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    int nextId_ = 1;
    std::atomic<bool> running_{true};

    std::unique_ptr<WarpManager> warps_;
    std::unique_ptr<RestApi> rest_;
    std::unique_ptr<ExtensionManager> ext_;
    PlayerRuleManager playerRules_;

    void acceptLoop(socket_t listener, int port);
    void exportClientRtt(std::shared_ptr<Session> s);
    void handleClient(std::shared_ptr<Session> s);
    bool openBackend(std::shared_ptr<Session> s, const RoutingServer& t, bool isSwitch);
    void closeSock(socket_t& s);
    void setNodelay(socket_t s);

    void pipeClientToServer(std::shared_ptr<Session> s);
    void pipeServerToClient(std::shared_ptr<Session> s, bool isSwitch, socket_t backendSocket, std::uint64_t generation);

    void clearOtherPlayers(std::shared_ptr<Session> s);
    void reinjectCharacter(std::shared_ptr<Session> s, uint8_t slot, socket_t backendSocket, std::uint64_t generation);
    void sendSectionsAndSpawn(std::shared_ptr<Session> s, socket_t backendSocket, std::uint64_t generation);
    void applyDebuffs(std::shared_ptr<Session> s);
    void onBackendLost(std::shared_ptr<Session> s, const std::string& reason, bool force, const std::vector<uint8_t>& kickFrame = {});

    void consoleLoop();
};
