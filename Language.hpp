#pragma once
#include <map>
#include <string>

// English-only language pack. The boolean parameter is kept for source compatibility.
inline std::map<std::string, std::string> languageDefaults(bool /* en */) {
    return {
        {"areYouEvenConnected", "[i:4076][c/d62ef0:Dimensions] Are you connected?"},
        {"availableDimensions", "[i:4076][c/d62ef0:Dimensions] [c/f88efa:Available dimensions:]"},
        {"blacklistCheckError", "Blacklist check failed."},
        {"blacklisted", "You are blacklisted."},
        {"characterNameLengthOutOfRange", "Character name must contain 2 to 20 characters."},
        {"close", "Server restarting. Please reconnect."},
        {"connectionRateLimitExceeded", "Too many connections. Please wait."},
        {"currentTcpRtt", "[i:4076][c/d62ef0:Dimensions] Your network ping: ${overallRtt}"},
        {"dimensionDisconnectedYou", "[i:4076][c/d62ef0:Dimensions] disconnected you."},
        {"dimensionDropped", "Connection to your dimension was lost."},
        {"dimensionsCommandName", "dim"},
        {"invalidPacketLength", "Invalid client packet length."},
        {"nameAlreadyOnServer", "Player ${name} is already on the server."},
        {"noServersAvailable", "Servers are unavailable. Please try again later."},
        {"playerCount", "[i:4076][c/d62ef0:Dimensions] [c/f88efa:${total} players online.]"},
        {"reason", " "},
        {"shiftingToDimension", "[i:4076][c/d62ef0:Dimensions] [c/8efad2:Shifting to dimension] ${name}..."},
        {"specifyADimensionToTravel", "Specify a dimension to travel to:"},
        {"tcpRttUnavailable", "[i:4076][c/d62ef0:Dimensions] [c/ff3333:Error:] TCP RTT is unavailable."},
        {"youAreAlreadyInthatDimension", "[i:4076][c/d62ef0:Dimensions] [c/f88efa:You are already in this dimension.]"},
        {"youEnteredTheVoid", "[i:4076][c/d62ef0:Dimensions] [c/f88efa:You entered the Void. You will disappear soon.]"},
        {"youNeedToWaitUntilConnected", "[i:4076][c/d62ef0:Dimensions] Please wait for the current connection to complete."}
    };
}
