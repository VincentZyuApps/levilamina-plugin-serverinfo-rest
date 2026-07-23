#include "mod/PlayerSnapshot.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

namespace serverinfo_rest {
namespace {

TEST(PlayerSnapshotTest, SerializesRichPlayerDetailsWithoutSensitiveConnectionIdentifiers) {
    PlayerSnapshot snapshot;
    snapshot.name = "Steve";
    snapshot.xuid = "123";
    snapshot.uuid = "uuid";
    snapshot.uniqueId = 42;
    snapshot.locale = "zh_CN";
    snapshot.permissionLevel = {2, "admin"};
    snapshot.gameMode = {6, "spectator"};
    snapshot.health = 18;
    snapshot.maxHealth = 20;
    snapshot.position = {1.5, 64.0, -2.5, 0};
    snapshot.blockPosition = {1, 64, -3, 0};
    snapshot.feetPosition = {1.5, 63.4, -2.5, 0};
    snapshot.mainHand = SnapshotItem{"minecraft:diamond_pickaxe", "Diamond Pickaxe", 1, true};
    snapshot.device.platform = {0, "desktop"};
    snapshot.network = SnapshotNetwork{12, 15, 0.01, 0.02};
    snapshot.snapshotAtMs = 123456;

    const auto json = playerSnapshotToJson(snapshot);
    EXPECT_EQ(json.at("name"), "Steve");
    EXPECT_EQ(json.at("uniqueId"), "42");
    EXPECT_EQ(json.at("gameMode").at("value"), 6);
    EXPECT_EQ(json.at("gameMode").at("name"), "spectator");
    EXPECT_EQ(json.at("position").at("dimensionId"), 0);
    EXPECT_EQ(json.at("mainHand").at("typeName"), "minecraft:diamond_pickaxe");
    EXPECT_EQ(json.at("network").at("averagePingMs"), 15);
    EXPECT_TRUE(json.at("offHand").is_null());
    EXPECT_TRUE(json.at("lastDeathPosition").is_null());
    EXPECT_FALSE(json.contains("ipAndPort"));
    const auto serialized = json.dump();
    EXPECT_EQ(serialized.find("clientId"), std::string::npos);
    EXPECT_EQ(serialized.find("serverAddress"), std::string::npos);
}

} // namespace
} // namespace serverinfo_rest
