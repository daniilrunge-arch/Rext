#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <optional>
#include <tuple>
#include <set>
#include <algorithm>

// Полный список пакетов Terraria (как в Python packets.py + JS packettypes.js)
enum class PacketType : uint8_t {
    ConnectRequest = 1,
    Disconnect = 2,
    ContinueConnecting = 3,
    PlayerInfo = 4,
    PlayerInventorySlot = 5,
    ContinueConnecting2 = 6,
    WorldInfo = 7,
    GetSectionOrRequestSync = 8,
    Status = 9,
    SendSection = 10,
    SectionTileFrame = 11,
    SpawnPlayer = 12,
    UpdatePlayer = 13,
    PlayerActive = 14,
    Null = 15,
    PlayerHP = 16,
    ModifyTile = 17,
    Time = 18,
    DoorToggle = 19,
    SendTileSquare = 20,
    UpdateItemDrop = 21,
    UpdateItemOwner = 22,
    NPCUpdate = 23,
    StrikeNPCwithHeldItem = 24,
    ChatMessage = 25,
    PlayerDamage = 26,
    ProjectileUpdate = 27,
    NPCStrike = 28,
    DestroyProjectile = 29,
    TogglePVP = 30,
    GetChestContentsOrActiveChest = 31,
    ChestItem = 32,
    SetChestNameOrOpenChest = 33,
    PlaceOrKillChest = 34,
    HealEffect = 35,
    PlayerZone = 36,
    RequestPassword = 37,
    SendPassword = 38,
    RemoveItemOwner = 39,
    SetActiveNPC = 40,
    PlayerItemAnimation = 41,
    PlayerMana = 42,
    ManaEffect = 43,
    KillMe = 44,
    PlayerTeam = 45,
    RequestSign = 46,
    UpdateOrDisplaySign = 47,
    SetLiquid = 48,
    CompleteConnectionAndSpawn = 49,
    UpdatePlayerBuff = 50,
    SpecialNPCEffect = 51,
    Unlock = 52,
    AddNPCBuff = 53,
    UpdateNPCBuff = 54,
    AddPlayerBuff = 55,
    UpdateNPCName = 56,
    UpdateGoodOrEvil = 57,
    PlayMusicItem = 58,
    HitSwitch = 59,
    NPCHomeUpdate = 60,
    SpawnBossOrInvasion = 61,
    PlayerDodge = 62,
    PaintTile = 63,
    PaintWall = 64,
    PlayerOrNPCTeleport = 65,
    HealOtherPlayer = 66,
    DimensionsUpdate = 67,
    ClientUUID = 68,
    GetChestName = 69,
    CatchNPC_bugs = 70,
    ReleaseNPC = 71,
    TravellingMerchantInventory = 72,
    TeleportationPotion = 73,
    AnglerQuest = 74,
    CompleteAnglerquesttoday = 75,
    NumberOfAnglerQuestsCompleted = 76,
    CreateTemporaryAnimation = 77,
    ReportInvasionProgress = 78,
    PlaceObject = 79,
    SyncPlayerChestIndex = 80,
    CreateCombatText = 81,
    LoadNetModule = 82,
    SetNPCKillCount = 83,
    SetPlayerStealth = 84,
    ForceItemIntoNearestChest = 85,
    UpdateTileEntity = 86,
    PlaceTileEntity = 87,
    AlterItemDrop = 88,
    PlaceItemFrame = 89,
    UpdateItemDrop_Instanced = 90,
    SyncEmoteBubble = 91,
    SyncExtraValue = 92,
    SocialHandshake = 93,
    Deprecated = 94,
    KillPortal = 95,
    PlayerTeleportThroughPortal = 96,
    NotifyPlayerNPCKilled = 97,
    NotifyPlayerOfEvent = 98,
    UpdateMinionTarget = 99,
    NPCTeleportThroughPortal = 100,
    UpdateShieldStrengths = 101,
    NebulaLevelUpRequest = 102,
    UpdateMoonLordCountdown = 103,
    SetNPCShopItem = 104,
    ToggleGemLock = 105,
    PoofofSmoke = 106,
    SmartChatMessage = 107,
    WiredCannonShot = 108,
    MassWireOperation = 109,
    MassWireOperationConsume = 110,
    ToggleBirthdayParty = 111,
    GrowFX = 112,
    CrystalInvasionStart = 113,
    CrystalInvasionWipeAll = 114,
    MinionAttackTargetUpdate = 115,
    CrystalInvasionSendWaitTime = 116,
    PlayerHurtV2 = 117,
    PlayerDeathV2 = 118,
    CombatTextString = 119,
    Emoji = 120,
    TEDisplayDollItemSync = 121,
    RequestTileEntityInteraction = 122,
    WeaponsRackTryPlacing = 123,
    TEHatRackItemSync = 124,
    SyncTilePicking = 125,
    SyncRevengeMarker = 126,
    RemoveRevengeMarker = 127,
    LandGolfBallInCup = 128,
    FinishedConnectingToServer = 129,
    FishOutNPC = 130,
    TamperWithNPC = 131,
    PlayLegacySound = 132,
    FoodPlatterTryPlacing = 133,
    UpdatePlayerLuckFactors = 134,
    DeadPlayer = 135,
    SyncCavernMonsterType = 136,
    RequestNPCBuffRemoval = 137,
    ClientSyncedInventory = 138,
    SetCountsAsHostForGameplay = 139,
    CreditsOrSlimeTransform = 140,
    LucyAxeMessage = 141,
    PiggyBankVoidLensUpdate = 142,
    DungeonDefendersEventAttemptSkipWait = 143,
    HaveDryadDoStardewAnimation = 144,
    ItemDropShimmeredUpdate = 145,
    ShimmerEffectOrCoinLuck = 146,
    LoadoutSwitch = 147,
    ItemDropProtectedUpdate = 148,
    DeadCellsDisplayJarTryPlacing = 149,
    PlayerSpectate = 150,
    ItemDropClear = 151,
    PlayerItemUseSound = 152,
    NpcHurtByDebuff = 153,
    Ping = 154,
    ChestResize = 155,
    LeashedEntityAnchorInsertItem = 156,
    PlayerTeamUpdate = 157,
    PlayerTeamSwapSpawn = 158,
    SectionRequest = 159,
    ItemDropPosition = 160,
    HostToken = 161
};

inline const char* packetTypeName(std::uint8_t id) {
    static const char* const names[] = {
        "Unknown", "ConnectRequest", "Disconnect", "ContinueConnecting",
        "PlayerInfo", "PlayerInventorySlot", "ContinueConnecting2", "WorldInfo",
        "GetSectionOrRequestSync", "Status", "SendSection", "SectionTileFrame",
        "SpawnPlayer", "UpdatePlayer", "PlayerActive", "Null",
        "PlayerHP", "ModifyTile", "Time", "DoorToggle",
        "SendTileSquare", "UpdateItemDrop", "UpdateItemOwner", "NPCUpdate",
        "StrikeNPCwithHeldItem", "ChatMessage", "PlayerDamage", "ProjectileUpdate",
        "NPCStrike", "DestroyProjectile", "TogglePVP", "GetChestContentsOrActiveChest",
        "ChestItem", "SetChestNameOrOpenChest", "PlaceOrKillChest", "HealEffect",
        "PlayerZone", "RequestPassword", "SendPassword", "RemoveItemOwner",
        "SetActiveNPC", "PlayerItemAnimation", "PlayerMana", "ManaEffect",
        "KillMe", "PlayerTeam", "RequestSign", "UpdateOrDisplaySign",
        "SetLiquid", "CompleteConnectionAndSpawn", "UpdatePlayerBuff", "SpecialNPCEffect",
        "Unlock", "AddNPCBuff", "UpdateNPCBuff", "AddPlayerBuff",
        "UpdateNPCName", "UpdateGoodOrEvil", "PlayMusicItem", "HitSwitch",
        "NPCHomeUpdate", "SpawnBossOrInvasion", "PlayerDodge", "PaintTile",
        "PaintWall", "PlayerOrNPCTeleport", "HealOtherPlayer", "DimensionsUpdate",
        "ClientUUID", "GetChestName", "CatchNPC_bugs", "ReleaseNPC",
        "TravellingMerchantInventory", "TeleportationPotion", "AnglerQuest", "CompleteAnglerquesttoday",
        "NumberOfAnglerQuestsCompleted", "CreateTemporaryAnimation", "ReportInvasionProgress", "PlaceObject",
        "SyncPlayerChestIndex", "CreateCombatText", "LoadNetModule", "SetNPCKillCount",
        "SetPlayerStealth", "ForceItemIntoNearestChest", "UpdateTileEntity", "PlaceTileEntity",
        "AlterItemDrop", "PlaceItemFrame", "UpdateItemDrop_Instanced", "SyncEmoteBubble",
        "SyncExtraValue", "SocialHandshake", "Deprecated", "KillPortal",
        "PlayerTeleportThroughPortal", "NotifyPlayerNPCKilled", "NotifyPlayerOfEvent", "UpdateMinionTarget",
        "NPCTeleportThroughPortal", "UpdateShieldStrengths", "NebulaLevelUpRequest", "UpdateMoonLordCountdown",
        "SetNPCShopItem", "ToggleGemLock", "PoofofSmoke", "SmartChatMessage",
        "WiredCannonShot", "MassWireOperation", "MassWireOperationConsume", "ToggleBirthdayParty",
        "GrowFX", "CrystalInvasionStart", "CrystalInvasionWipeAll", "MinionAttackTargetUpdate",
        "CrystalInvasionSendWaitTime", "PlayerHurtV2", "PlayerDeathV2", "CombatTextString",
        "Emoji", "TEDisplayDollItemSync", "RequestTileEntityInteraction", "WeaponsRackTryPlacing",
        "TEHatRackItemSync", "SyncTilePicking", "SyncRevengeMarker", "RemoveRevengeMarker",
        "LandGolfBallInCup", "FinishedConnectingToServer", "FishOutNPC", "TamperWithNPC",
        "PlayLegacySound", "FoodPlatterTryPlacing", "UpdatePlayerLuckFactors", "DeadPlayer",
        "SyncCavernMonsterType", "RequestNPCBuffRemoval", "ClientSyncedInventory", "SetCountsAsHostForGameplay",
        "CreditsOrSlimeTransform", "LucyAxeMessage", "PiggyBankVoidLensUpdate", "DungeonDefendersEventAttemptSkipWait",
        "HaveDryadDoStardewAnimation", "ItemDropShimmeredUpdate", "ShimmerEffectOrCoinLuck", "LoadoutSwitch",
        "ItemDropProtectedUpdate", "DeadCellsDisplayJarTryPlacing", "PlayerSpectate", "ItemDropClear",
        "PlayerItemUseSound", "NpcHurtByDebuff", "Ping", "ChestResize",
        "LeashedEntityAnchorInsertItem", "PlayerTeamUpdate", "PlayerTeamSwapSpawn", "SectionRequest",
        "ItemDropPosition", "HostToken"
    };
    return id < (sizeof(names) / sizeof(names[0])) ? names[id] : "Unknown";
}


inline std::vector<uint8_t> encodeCsharpString(const std::string& s) {
    std::vector<uint8_t> out;
    size_t len = s.size();
    while (len >= 0x80) {
        out.push_back(uint8_t((len & 0x7F) | 0x80));
        len >>= 7;
    }
    out.push_back(uint8_t(len));
    out.insert(out.end(), s.begin(), s.end());
    return out;
}

inline std::string readCsharpString(const uint8_t* data, size_t size, size_t& offset) {
    if (offset >= size) return {};
    size_t length = 0, shift = 0;
    while (offset < size) {
        uint8_t b = data[offset++];
        length |= size_t(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift > 35) break;
    }
    if (offset + length > size) return {};
    std::string s(reinterpret_cast<const char*>(data + offset), length);
    offset += length;
    return s;
}

inline std::vector<uint8_t> framePacket(const std::vector<uint8_t>& body) {
    uint16_t len = uint16_t(body.size() + 2);
    std::vector<uint8_t> out(2 + body.size());
    out[0] = uint8_t(len & 0xFF);
    out[1] = uint8_t((len >> 8) & 0xFF);
    if (!body.empty())
        std::memcpy(out.data() + 2, body.data(), body.size());
    return out;
}

inline std::vector<uint8_t> buildConnectRequest(const std::string& ver = "Terraria326") {
    auto p = encodeCsharpString(ver);
    std::vector<uint8_t> body = { static_cast<uint8_t>(PacketType::ConnectRequest) };
    body.insert(body.end(), p.begin(), p.end());
    return framePacket(body);
}

inline std::vector<uint8_t> buildDisconnect(const std::string& reason) {
    // Packet 2: NetworkText mode 0 + string
    auto p = encodeCsharpString(reason);
    std::vector<uint8_t> body = { static_cast<uint8_t>(PacketType::Disconnect), 0 };
    body.insert(body.end(), p.begin(), p.end());
    return framePacket(body);
}

// Packet 9 Status — кастомный текст загрузки (как в Dimensions / плагинах)
inline std::vector<uint8_t> buildStatus(const std::string& text, int32_t max = 0, bool hidePercent = true) {
    auto p = encodeCsharpString(text);
    std::vector<uint8_t> body;
    body.push_back(static_cast<uint8_t>(PacketType::Status));
    // int32 max
    body.push_back(uint8_t(max & 0xFF));
    body.push_back(uint8_t((max >> 8) & 0xFF));
    body.push_back(uint8_t((max >> 16) & 0xFF));
    body.push_back(uint8_t((max >> 24) & 0xFF));
    // NetworkText mode 0
    body.push_back(0);
    body.insert(body.end(), p.begin(), p.end());
    // flags: bit0 hideStatusTextPercent, bit1 statusTextHasShadows
    uint8_t flags = 0;
    if (hidePercent) flags |= 1;
    flags |= 2; // shadows
    body.push_back(flags);
    return framePacket(body);
}

inline std::vector<uint8_t> buildChatMessage(const std::string& text,
    uint8_t r = 255, uint8_t g = 255, uint8_t b = 100) {
    // NetModule Say (module id 1)
    std::vector<uint8_t> payload = {1, 0, 255, 0}; // moduleId=1 little, author=255
    auto msg = encodeCsharpString(text);
    payload.insert(payload.end(), msg.begin(), msg.end());
    payload.push_back(r); payload.push_back(g); payload.push_back(b);
    std::vector<uint8_t> body = { static_cast<uint8_t>(PacketType::LoadNetModule) };
    body.insert(body.end(), payload.begin(), payload.end());
    return framePacket(body);
}

inline std::vector<uint8_t> buildRequestWorldData() {
    return framePacket({ static_cast<uint8_t>(PacketType::ContinueConnecting2) });
}

inline std::vector<uint8_t> buildGetSection(int32_t x = -1, int32_t y = -1) {
    std::vector<uint8_t> body(10);
    body[0] = static_cast<uint8_t>(PacketType::GetSectionOrRequestSync);
    std::memcpy(body.data() + 1, &x, 4);
    std::memcpy(body.data() + 5, &y, 4);
    body[9] = 0;
    return framePacket(body);
}

inline std::vector<uint8_t> buildPlayerActive(uint8_t id, bool active) {
    return framePacket({ static_cast<uint8_t>(PacketType::PlayerActive), id, uint8_t(active ? 1 : 0) });
}

inline std::vector<uint8_t> buildSpawnPlayer(uint8_t id, int32_t sx = -1, int32_t sy = -1) {
    std::vector<uint8_t> body(16, 0);
    body[0] = static_cast<uint8_t>(PacketType::SpawnPlayer);
    body[1] = id;
    const int16_t x = static_cast<int16_t>(sx), y = static_cast<int16_t>(sy);
    std::memcpy(body.data() + 2, &x, 2);
    std::memcpy(body.data() + 4, &y, 2);
    // respawn i32, deaths PvE/PvP i16, context u8, team u8 (326)
    body[15] = 1; // team at 14, spawn context at 15
    return framePacket(body);
}

inline std::vector<uint8_t> buildAddPlayerBuff(uint8_t slot, uint16_t buffId, int32_t ticks) {
    std::vector<uint8_t> body(8);
    body[0] = static_cast<uint8_t>(PacketType::AddPlayerBuff);
    body[1] = slot;
    std::memcpy(body.data() + 2, &buffId, 2);
    std::memcpy(body.data() + 4, &ticks, 4);
    return framePacket(body);
}

inline std::vector<uint8_t> buildSyncPlayerBuffs(uint8_t slot, const std::vector<uint16_t>& buffs) {
    std::vector<uint8_t> body{50,slot};
    for(auto buff:buffs) if(buff) {body.push_back(uint8_t(buff));body.push_back(uint8_t(buff>>8));}
    body.push_back(0);body.push_back(0);
    return framePacket(body);
}

// Protocol 326: slot, skin, voice, pitch(float), hair, BinaryWriter name.
inline std::optional<std::string> extractPlayerName(const uint8_t* f, size_t n) {
    if(n<12 || f[2]!=4)return std::nullopt;
    size_t end=11;auto name=readCsharpString(f,n,end);
    if(name.empty() || name.size()>80 || end>n)return std::nullopt;
    return name;
}
inline bool rewritePlayerName(std::vector<uint8_t>& f,const std::string& name){
    if(!extractPlayerName(f.data(),f.size()))return false;
    size_t end=11;readCsharpString(f.data(),f.size(),end);
    auto encoded=encodeCsharpString(name);
    f.erase(f.begin()+11,f.begin()+end);f.insert(f.begin()+11,encoded.begin(),encoded.end());
    f[0]=uint8_t(f.size());f[1]=uint8_t(f.size()>>8);return true;
}

inline std::optional<std::string> extractChatText(const uint8_t* f, size_t n) {
    // Terraria 1.4.5.x packet 82: u16 module id, then two C# strings
    // (command name, text). The command normally arrives as "Say" (capital S).
    if (!f || n < 7 || f[2] != static_cast<uint8_t>(PacketType::LoadNetModule))
        return std::nullopt;
    const uint16_t mod = uint16_t(f[3]) | (uint16_t(f[4]) << 8);
    if (mod != 1) return std::nullopt; // NetTextModule

    auto lowerAscii = [](std::string value) {
        for (char& c : value) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u >= 'A' && u <= 'Z') c = static_cast<char>(u - 'A' + 'a');
        }
        return value;
    };

    size_t offset = 5;
    const std::string command = readCsharpString(f, n, offset);
    const std::string text = readCsharpString(f, n, offset);
    if (offset <= n && !text.empty() && lowerAscii(command) == "say")
        return text;

    // Be tolerant of older/custom clients that send a slash command as the first string.
    offset = 5;
    const std::string first = readCsharpString(f, n, offset);
    if (!first.empty() && (first[0] == '/' || first[0] == '.')) return first;
    return std::nullopt;
}

inline std::optional<std::string> extractDisconnectReason(const uint8_t* f, size_t n) {
    if (n < 4 || f[2] != static_cast<uint8_t>(PacketType::Disconnect)) return std::nullopt;
    // Terraria disconnect payload is NetworkText: mode byte, then string.
    size_t off = (n >= 5 && f[3] <= 2) ? 4 : 3;
    auto r = readCsharpString(f, n, off);
    if (r.empty()) return std::nullopt;
    return r;
}

// Все пакеты, где PlayerId идёт первым байтом полезной нагрузки (offset 3)
inline bool isPlayerIdPacket(uint8_t id) {
    static const std::set<uint8_t> ids = {
        static_cast<uint8_t>(PacketType::PlayerInfo),
        static_cast<uint8_t>(PacketType::PlayerInventorySlot),
        static_cast<uint8_t>(PacketType::SpawnPlayer),
        static_cast<uint8_t>(PacketType::UpdatePlayer),
        static_cast<uint8_t>(PacketType::PlayerActive),
        static_cast<uint8_t>(PacketType::PlayerHP),
        static_cast<uint8_t>(PacketType::PlayerZone),
        static_cast<uint8_t>(PacketType::PlayerItemAnimation),
        static_cast<uint8_t>(PacketType::PlayerMana),
        static_cast<uint8_t>(PacketType::PlayerTeam),
        static_cast<uint8_t>(PacketType::UpdatePlayerBuff),
        static_cast<uint8_t>(PacketType::PlayerDodge),
        static_cast<uint8_t>(PacketType::SetPlayerStealth),
        static_cast<uint8_t>(PacketType::PlayerTeleportThroughPortal),
        static_cast<uint8_t>(PacketType::PlayerHurtV2),
        static_cast<uint8_t>(PacketType::PlayerDeathV2),
        static_cast<uint8_t>(PacketType::SyncTilePicking),   // критично для добычи
        static_cast<uint8_t>(PacketType::LoadoutSwitch),
        static_cast<uint8_t>(PacketType::PlayerSpectate),
        static_cast<uint8_t>(PacketType::PlayerItemUseSound),
        static_cast<uint8_t>(PacketType::PlayerTeamSwapSpawn),
        static_cast<uint8_t>(PacketType::AddPlayerBuff)
    };
    return ids.count(id) > 0;
}

inline void patchPlayerSlot(std::vector<uint8_t>& frame, uint8_t slot) {
    if (frame.size() < 4) return;
    if (isPlayerIdPacket(frame[2])) frame[3] = slot;
}


// WorldInfo (7): spawnX/spawnY как int16 после time+flags+maxTiles
// layout after type: time i32, dayTime u8, moonPhase u8, bloodMoon u8, eclipse u8,
// maxTilesX u16, maxTilesY u16, spawnX i16, spawnY i16
inline bool extractWorldSpawn(const uint8_t* f, size_t n, int& sx, int& sy) {
    if (!f || n < 17) return false;
    // f[2] == type, payload starts at 3
    size_t o = 3;
    o += 4; // time
    o += 1 + 1; // packed day/blood/eclipse flags, moon phase
    o += 2 + 2; // maxTilesX/Y
    if (o + 4 > n) return false;
    int16_t x = 0, y = 0;
    std::memcpy(&x, f + o, 2);
    std::memcpy(&y, f + o + 2, 2);
    sx = int(x);
    sy = int(y);
    return true;
}

inline bool extractPlayerPos(const uint8_t* f, size_t n, float& px, float& py) {
    if (n < 17 || f[2] != static_cast<uint8_t>(PacketType::UpdatePlayer)) return false;
    std::memcpy(&px, f + 9, 4);
    std::memcpy(&py, f + 13, 4);
    return true;
}

// Пакеты, которые нельзя пропускать на бэкенд во время switch (античит TShock / потеря блоков)
inline bool isWorldModifyPacket(uint8_t id) {
    // These frames must be handled by Terraria/TShock itself. The proxy must not
    // reinterpret, consume, or delay world-edit actions.
    return id == static_cast<uint8_t>(PacketType::ModifyTile) ||
           id == static_cast<uint8_t>(PacketType::SyncTilePicking) ||
           id == static_cast<uint8_t>(PacketType::SectionTileFrame) ||
           id == static_cast<uint8_t>(PacketType::SendTileSquare) ||
           id == static_cast<uint8_t>(PacketType::PlaceObject) ||
           id == static_cast<uint8_t>(PacketType::PaintTile) ||
           id == static_cast<uint8_t>(PacketType::PaintWall) ||
           id == static_cast<uint8_t>(PacketType::SetLiquid);
}

inline bool isChestInteractionPacket(uint8_t id) {
    // Chest open/inventory/name/placement/resize are deliberately left entirely
    // to the backend game server. No proxy-side chest state is maintained.
    return id == static_cast<uint8_t>(PacketType::GetChestContentsOrActiveChest) ||
           id == static_cast<uint8_t>(PacketType::ChestItem) ||
           id == static_cast<uint8_t>(PacketType::SetChestNameOrOpenChest) ||
           id == static_cast<uint8_t>(PacketType::PlaceOrKillChest) ||
           id == static_cast<uint8_t>(PacketType::GetChestName) ||
           id == static_cast<uint8_t>(PacketType::ChestResize);
}
