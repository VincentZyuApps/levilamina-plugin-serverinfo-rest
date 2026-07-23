#include "mod/PlayerSnapshot.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace serverinfo_rest {

namespace {

nlohmann::json enumValueToJson(const SnapshotEnumValue& value) {
    return {{"value", value.value}, {"name", value.name}};
}

nlohmann::json positionToJson(const SnapshotPosition& position) {
    nlohmann::json json = {{"x", position.x}, {"y", position.y}, {"z", position.z}};
    if (position.dimensionId) json["dimensionId"] = *position.dimensionId;
    return json;
}

nlohmann::json blockPositionToJson(const SnapshotBlockPosition& position) {
    nlohmann::json json = {{"x", position.x}, {"y", position.y}, {"z", position.z}};
    if (position.dimensionId) json["dimensionId"] = *position.dimensionId;
    return json;
}

nlohmann::json itemToJson(const SnapshotItem& item) {
    return {
        {"typeName", item.typeName},
        {"displayName", item.displayName},
        {"count", item.count},
        {"enchanted", item.enchanted},
    };
}

} // namespace

nlohmann::json playerSnapshotToJson(const PlayerSnapshot& snapshot) {
    nlohmann::json armor = nlohmann::json::array();
    for (const auto& entry : snapshot.armor) {
        armor.push_back({{"slot", entry.slot}, {"item", itemToJson(entry.item)}});
    }

    nlohmann::json json = {
        {"name", snapshot.name},
        {"xuid", snapshot.xuid},
        {"uuid", snapshot.uuid},
        {"uniqueId", std::to_string(snapshot.uniqueId)},
        {"locale", snapshot.locale},
        {"permissionLevel", enumValueToJson(snapshot.permissionLevel)},
        {"isOperator", snapshot.isOperator},
        {"isSimulated", snapshot.isSimulated},
        {"gameMode", enumValueToJson(snapshot.gameMode)},
        {"health", snapshot.health},
        {"maxHealth", snapshot.maxHealth},
        {"speed", snapshot.speed},
        {"isFlying", snapshot.isFlying},
        {"isSneaking", snapshot.isSneaking},
        {"isSprinting", snapshot.isSprinting},
        {"isMoving", snapshot.isMoving},
        {"isInWater", snapshot.isInWater},
        {"isInLava", snapshot.isInLava},
        {"isOnGround", snapshot.isOnGround},
        {"isOnFire", snapshot.isOnFire},
        {"isSleeping", snapshot.isSleeping},
        {"isGliding", snapshot.isGliding},
        {"isRiding", snapshot.isRiding},
        {"isInvisible", snapshot.isInvisible},
        {"canFly", snapshot.canFly},
        {"canSleep", snapshot.canSleep},
        {"position", positionToJson(snapshot.position)},
        {"blockPosition", blockPositionToJson(snapshot.blockPosition)},
        {"feetPosition", positionToJson(snapshot.feetPosition)},
        {"lastDeathPosition", snapshot.lastDeathPosition
            ? blockPositionToJson(*snapshot.lastDeathPosition)
            : nlohmann::json(nullptr)},
        {"respawnPosition", snapshot.respawnPosition
            ? blockPositionToJson(*snapshot.respawnPosition)
            : nlohmann::json(nullptr)},
        {"rotation", {{"pitch", snapshot.rotation.pitch}, {"yaw", snapshot.rotation.yaw}}},
        {"biome", snapshot.biome
            ? nlohmann::json{{"id", snapshot.biome->id}, {"name", snapshot.biome->name}}
            : nlohmann::json(nullptr)},
        {"standingOn", snapshot.standingOn
            ? nlohmann::json{
                {"typeName", snapshot.standingOn->typeName},
                {"descriptionId", snapshot.standingOn->descriptionId},
            }
            : nlohmann::json(nullptr)},
        {"expNeededForNextLevel", snapshot.expNeededForNextLevel},
        {"mainHand", snapshot.mainHand ? itemToJson(*snapshot.mainHand) : nlohmann::json(nullptr)},
        {"offHand", snapshot.offHand ? itemToJson(*snapshot.offHand) : nlohmann::json(nullptr)},
        {"armor", std::move(armor)},
        {"device", {
            {"platform", enumValueToJson(snapshot.device.platform)},
            {"inputMode", snapshot.device.inputMode
                ? enumValueToJson(*snapshot.device.inputMode)
                : nlohmann::json(nullptr)},
        }},
        {"network", snapshot.network
            ? nlohmann::json{
                {"currentPingMs", snapshot.network->currentPingMs},
                {"averagePingMs", snapshot.network->averagePingMs},
                {"currentPacketLoss", snapshot.network->currentPacketLoss},
                {"averagePacketLoss", snapshot.network->averagePacketLoss},
            }
            : nlohmann::json(nullptr)},
        {"snapshotAtMs", snapshot.snapshotAtMs},
    };
    return json;
}

} // namespace serverinfo_rest
