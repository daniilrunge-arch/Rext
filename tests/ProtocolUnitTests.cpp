#include "Packets.hpp"
#include "Framer.hpp"
#include "Config.hpp"
#include "PlayerRules.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>

static std::vector<std::uint8_t> clientSayPacket(const std::string& text) {
    std::vector<std::uint8_t> body{static_cast<std::uint8_t>(PacketType::LoadNetModule), 1, 0};
    auto command = encodeCsharpString("Say");
    auto payload = encodeCsharpString(text);
    body.insert(body.end(), command.begin(), command.end());
    body.insert(body.end(), payload.begin(), payload.end());
    return framePacket(body);
}

int main() {
    // Frame parser must preserve gameplay packets byte-for-byte, including tile/chest actions.
    {
        const std::vector<std::uint8_t> ids = {17, 31, 33, 34, 125};
        std::vector<std::vector<std::uint8_t>> expected;
        std::vector<std::uint8_t> stream;
        for (const auto id : ids) {
            auto packet = framePacket({id, 0x11, 0x22, 0x33, 0x44, 0x55});
            expected.push_back(packet);
            stream.insert(stream.end(), packet.begin(), packet.end());
        }
        TerrariaFramer framer;
        std::vector<std::vector<std::uint8_t>> actual;
        // Feed fragmented TCP reads and verify no frame loss or byte rewrite.
        for (std::size_t offset = 0; offset < stream.size();) {
            const std::size_t n = (std::min<std::size_t>)(3 + (offset % 7), stream.size() - offset);
            framer.feed(stream.data() + offset, n, actual);
            offset += n;
        }
        assert(actual == expected);
    }

    // The proxy must classify world edits and chest packets as backend-owned actions.
    {
        for (const auto id : {std::uint8_t(17), std::uint8_t(79), std::uint8_t(63),
                              std::uint8_t(64), std::uint8_t(48), std::uint8_t(125)})
            assert(isWorldModifyPacket(id));
        for (const auto id : {std::uint8_t(31), std::uint8_t(32), std::uint8_t(33),
                              std::uint8_t(34), std::uint8_t(69), std::uint8_t(155)})
            assert(isChestInteractionPacket(id));
        assert(!isChestInteractionPacket(17));
        assert(!isWorldModifyPacket(25));
    }

    // Configurable exact-reserved nickname list parsing.
    {
        std::vector<std::string> reserved;
        assert(cfg_util::findStringArray("{\"reservedNicknames\":[\"admin\", \"server\"]}",
                                         "reservedNicknames", reserved));
        assert(reserved.size() == 2 && reserved[0] == "admin" && reserved[1] == "server");
    }

    for (const std::string& command : {std::string("/servers"), std::string("/join Survival"), std::string("/warp PvE")}) {
        const auto frame = clientSayPacket(command);
        const auto decoded = extractChatText(frame.data(), frame.size());
        assert(decoded.has_value());
        assert(*decoded == command);
    }

    const std::string path = "protocol_test_rules.db";
    std::remove(path.c_str());
    {
        PlayerRuleManager rules(path);
        const auto duration = PlayerRuleManager::parseDuration("2h");
        assert(duration.has_value() && *duration == 7200);
        assert(!PlayerRuleManager::parseDuration("0h").has_value());
        assert(!PlayerRuleManager::parseDuration("9999d").has_value());
        assert(rules.grant("Nene", "warp", *duration));
        assert(rules.has("nene", "warp"));
        assert(!rules.has("someone", "warp"));
        assert(rules.grant("TestGuy", "all", *duration));
        assert(rules.has("testguy", "warp"));
    }
    {
        PlayerRuleManager reloaded(path);
        assert(reloaded.has("NENE", "warp"));
        assert(reloaded.revoke("nene", "warp"));
        assert(!reloaded.has("nene", "warp"));
    }
    std::remove(path.c_str());

    std::cout << "Protocol, frame parsing, and temporary-rule tests passed.\n";
    return 0;
}
