#pragma once
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <optional>
#include <cstdint>
#include <mutex>
#include "EntityState.hpp"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <netdb.h>
  using socket_t = int;
  constexpr socket_t INVALID_SOCK = -1;
#endif

struct Session {
    EntityState entities;
    int listenerPort=0;
    std::atomic<int64_t> rttSampleMs{0};
    int id = 0;
    socket_t client = INVALID_SOCK;
    socket_t backend = INVALID_SOCK;
    std::string username;
    std::string currentServer;
    std::string voidReturnServer;
    std::atomic<bool> inVoid{false};
    std::string ip;

    // Written only by the client->server reader for this session.
    std::int64_t chatWindowStartMs = 0;
    unsigned chatMessagesInWindow = 0;
    std::int64_t lastChatMessageMs = 0;
    std::string lastChatMessage;
    std::int64_t lastAntiSpamNoticeMs = 0;

    std::atomic<bool> closed{false};
    std::atomic<bool> backendReady{false};
    // Incremented for each backend replacement; stale readers must never consume the new socket.
    std::atomic<std::uint64_t> backendGeneration{0};
    std::atomic<std::uint64_t> realIpAckGeneration{0};
    std::atomic<std::uint64_t> clientPacketCount{0};
    std::atomic<std::uint64_t> serverPacketCount{0};
    std::atomic<bool> switching{false};
    // Timestamp for the current backend handoff; 0 means no handoff is active.
    std::atomic<std::int64_t> switchingStartedMs{0};
    int playerSlot = -1;   // 0..254, -1 = not assigned

    // Cached character data for reinjection on dimension switch (как в Python)
    std::vector<uint8_t> cachedP4;      // PlayerInfo (4)
    std::vector<uint8_t> cachedHp;      // PlayerHP (16)
    std::vector<uint8_t> cachedMana;    // PlayerMana (42)
    std::vector<uint8_t> cachedBuffs;   // UpdatePlayerBuff (50)
    std::vector<uint8_t> cachedUuid;    // ClientUUID (68)
    std::vector<uint8_t> cachedSpawn;   // SpawnPlayer (12)
    std::map<int, std::vector<uint8_t>> cachedSlots; // inventory slots

    std::optional<std::pair<int,int>> worldSpawn;
    std::atomic<bool> charInjected{false};
    std::atomic<bool> sectionSent{false};
    std::atomic<bool> spawnSent{false};
    std::atomic<int>  tilePackets{0};
    std::atomic<bool> spawnComplete{false};

    // Warp
    double warpCooldownUntil = 0.0;
    std::atomic<bool> warpActive{false};

    // Active buffs for debuff-on-switch
    std::vector<uint16_t> activeBuffs;

    // Serialize complete Terraria frames: chat/status writers and packet pipes share sockets.
    std::mutex clientWriteMtx;
    std::mutex backendWriteMtx;
    // Mutex for cache writes from client pipe
    std::mutex cacheMtx;
};
