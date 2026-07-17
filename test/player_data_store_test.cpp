#include "mod/PlayerDataStore.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace serverinfo_rest {
namespace {

class PlayerDataStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("serverinfo-rest-test-" + std::to_string(nonce));
        std::filesystem::create_directories(directory);
        dataPath = directory / "player-data.json";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    }

    static void write(const std::filesystem::path& path, const std::string& content) {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream.good());
        stream << content;
        ASSERT_TRUE(stream.good());
    }

    std::filesystem::path directory;
    std::filesystem::path dataPath;
};

TEST_F(PlayerDataStoreTest, TracksPlayTimeStatisticsAndPagination) {
    PlayerDataStore store(dataPath);
    std::string error;
    ASSERT_TRUE(store.load(error)) << error;

    store.playerJoined("xuid-1", "uuid-1", "Alice", 1000);
    store.incrementBlocksMined("xuid-1", "Alice", 1500);
    store.incrementMobsKilled("xuid-1", "Alice", 1600);
    store.playerLeft("xuid-1", 4000);
    store.playerJoined("xuid-2", "uuid-2", "Bob", 5000);

    const auto alice = store.findPlayer("alice", 6000);
    ASSERT_TRUE(alice.has_value());
    EXPECT_EQ(alice->totalPlayMs, 3000);
    EXPECT_EQ(alice->blocksMined, 1u);
    EXPECT_EQ(alice->mobsKilled, 1u);

    const auto page = store.history(1, 1, 6000);
    EXPECT_EQ(page.total, 2u);
    ASSERT_EQ(page.players.size(), 1u);
    EXPECT_EQ(page.players.front().name, "Bob");
    EXPECT_EQ(page.players.front().totalPlayMs, 1000);
}

TEST_F(PlayerDataStoreTest, EnforcesBindingOwnershipAndKeepsAdminGrantOnUnbind) {
    PlayerDataStore store(dataPath);
    std::string error;
    ASSERT_TRUE(store.load(error)) << error;

    const auto first = store.bindWhitelist("qq", "bot-1", "user-1", "group-1", "Alice", 1000);
    ASSERT_TRUE(first.success);
    EXPECT_TRUE(first.created);
    EXPECT_FALSE(store.bindWhitelist("qq", "bot-1", "user-2", "group-1", "alice", 1001).success);
    EXPECT_FALSE(store.bindWhitelist("qq", "bot-1", "user-1", "group-1", "Bob", 1002).success);

    const auto [grant, created] = store.addAdminWhitelist("Alice", "admin", 1003);
    EXPECT_TRUE(created);
    EXPECT_EQ(grant.playerName, "Alice");
    ASSERT_TRUE(store.unbindWhitelist("qq", "bot-1", "user-1").has_value());
    EXPECT_TRUE(store.hasWhitelistAuthorization("Alice"));
    EXPECT_TRUE(store.authorizePlayer("Alice", "xuid-1", false, false));
    EXPECT_TRUE(store.authorizePlayer("RenamedAlice", "xuid-1", false, false));
}

TEST_F(PlayerDataStoreTest, SavesAtomicallyAndRecoversLastBackup) {
    PlayerDataStore store(dataPath);
    std::string error;
    ASSERT_TRUE(store.load(error)) << error;
    store.playerJoined("xuid-1", "uuid-1", "Alice", 1000);
    ASSERT_TRUE(store.save(error, true)) << error;

    store.playerJoined("xuid-2", "uuid-2", "Bob", 2000);
    ASSERT_TRUE(store.save(error, true)) << error;
    ASSERT_TRUE(std::filesystem::exists(store.backupPath()));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(dataPath.string() + ".tmp")));

    write(dataPath, "{broken json");
    PlayerDataStore recovered(dataPath);
    error.clear();
    ASSERT_TRUE(recovered.load(error)) << error;
    EXPECT_TRUE(recovered.isAvailable());
    EXPECT_TRUE(recovered.wasRecoveredFromBackup());
    EXPECT_TRUE(recovered.findPlayer("Alice", 3000).has_value());
    EXPECT_FALSE(recovered.findPlayer("Bob", 3000).has_value());
}

TEST_F(PlayerDataStoreTest, RefusesToOverwriteWhenPrimaryAndBackupAreDamaged) {
    write(dataPath, "not-json");
    write(std::filesystem::path(dataPath.string() + ".bak"), "also-not-json");

    PlayerDataStore store(dataPath);
    std::string error;
    EXPECT_FALSE(store.load(error));
    EXPECT_FALSE(store.isAvailable());
    EXPECT_NE(error.find("player data unavailable"), std::string::npos);
    error.clear();
    EXPECT_FALSE(store.save(error, true));
    EXPECT_NE(error.find("refusing to overwrite"), std::string::npos);

    std::size_t corruptFiles = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().find(".corrupt-") != std::string::npos) ++corruptFiles;
    }
    EXPECT_EQ(corruptFiles, 2u);
}

} // namespace
} // namespace serverinfo_rest
