#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace serverinfo_rest {

struct SnapshotEnumValue {
    int value = 0;
    std::string name;
};

struct SnapshotPosition {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::optional<int> dimensionId;
};

struct SnapshotBlockPosition {
    int x = 0;
    int y = 0;
    int z = 0;
    std::optional<int> dimensionId;
};

struct SnapshotRotation {
    double pitch = 0.0;
    double yaw = 0.0;
};

struct SnapshotBiome {
    int id = 0;
    std::string name;
};

struct SnapshotBlock {
    std::string typeName;
    std::string descriptionId;
};

struct SnapshotItem {
    std::string typeName;
    std::string displayName;
    int count = 0;
    bool enchanted = false;
};

struct SnapshotArmorItem {
    std::string slot;
    SnapshotItem item;
};

struct SnapshotNetwork {
    int currentPingMs = 0;
    int averagePingMs = 0;
    double currentPacketLoss = 0.0;
    double averagePacketLoss = 0.0;
};

struct SnapshotDevice {
    SnapshotEnumValue platform;
    std::optional<SnapshotEnumValue> inputMode;
};

struct PlayerSnapshot {
    std::string name;
    std::string xuid;
    std::string uuid;
    std::int64_t uniqueId = 0;
    std::string locale;
    SnapshotEnumValue permissionLevel;
    bool isOperator = false;
    bool isSimulated = false;
    SnapshotEnumValue gameMode;
    int health = 0;
    int maxHealth = 0;
    double speed = 0.0;
    bool isFlying = false;
    bool isSneaking = false;
    bool isSprinting = false;
    bool isMoving = false;
    bool isInWater = false;
    bool isInLava = false;
    bool isOnGround = false;
    bool isOnFire = false;
    bool isSleeping = false;
    bool isGliding = false;
    bool isRiding = false;
    bool isInvisible = false;
    bool canFly = false;
    bool canSleep = false;
    SnapshotPosition position;
    SnapshotBlockPosition blockPosition;
    SnapshotPosition feetPosition;
    std::optional<SnapshotBlockPosition> lastDeathPosition;
    std::optional<SnapshotBlockPosition> respawnPosition;
    SnapshotRotation rotation;
    std::optional<SnapshotBiome> biome;
    std::optional<SnapshotBlock> standingOn;
    int expNeededForNextLevel = 0;
    std::optional<SnapshotItem> mainHand;
    std::optional<SnapshotItem> offHand;
    std::vector<SnapshotArmorItem> armor;
    SnapshotDevice device;
    std::optional<SnapshotNetwork> network;
    std::int64_t snapshotAtMs = 0;
};

nlohmann::json playerSnapshotToJson(const PlayerSnapshot& snapshot);

} // namespace serverinfo_rest
