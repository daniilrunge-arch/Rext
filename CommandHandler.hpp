#pragma once

#include <string>
#include <memory>

class ProxyServer;
struct Session;

// Call when player name is known / leaves — updates last-seen for /play
void touchLastSeen(const std::string& name);

class CommandHandler {
public:
    static bool handle(ProxyServer& proxy, std::shared_ptr<Session> s, const std::string& textRaw);
};
