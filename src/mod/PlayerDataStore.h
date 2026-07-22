#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace serverinfo_rest {

struct PlayerRecord {
    std::string xuid;
    std::string uuid;
    std::string name;
    std::int64_t firstSeenMs = 0;
    std::int64_t lastSeenMs = 0;
    std::int64_t totalPlayMs = 0;
    std::uint64_t joinCount = 0;
    std::uint64_t blocksMined = 0;
    std::uint64_t mobsKilled = 0;
};

struct WhitelistBinding {
    std::string platform;
    std::string selfId;
    std::string userId;
    std::string channelId;
    std::string playerName;
    std::string xuid;
    std::int64_t boundAtMs = 0;
};

struct AdminWhitelistGrant {
    std::string playerName;
    std::string xuid;
    std::string addedBy;
    std::int64_t addedAtMs = 0;
};

struct PlayerHistoryPage {
    std::vector<PlayerRecord> players;
    std::size_t total = 0;
    int page = 1;
    int pageSize = 30;
};

struct BindingResult {
    bool success = false;
    bool created = false;
    std::string error;
    WhitelistBinding binding;
};

class PlayerDataStore {
public:
    explicit PlayerDataStore(std::filesystem::path filePath);

    bool load(std::string& error);
    bool save(std::string& error, bool force = false);
    [[nodiscard]] bool isAvailable() const { return mAvailable.load(); }
    [[nodiscard]] bool wasRecoveredFromBackup() const { return mRecoveredFromBackup.load(); }
    [[nodiscard]] std::filesystem::path backupPath() const;
    [[nodiscard]] std::vector<std::string> authorizedPlayerNames() const;

    void playerJoined(
        const std::string& xuid,
        const std::string& uuid,
        const std::string& name,
        std::int64_t nowMs
    );
    void playerLeft(const std::string& xuid, std::int64_t nowMs);
    void incrementBlocksMined(const std::string& xuid, const std::string& name, std::int64_t nowMs);
    void incrementMobsKilled(const std::string& xuid, const std::string& name, std::int64_t nowMs);

    PlayerHistoryPage history(int page, int pageSize, std::int64_t nowMs) const;
    std::optional<PlayerRecord> findPlayer(const std::string& nameOrXuid, std::int64_t nowMs) const;

    BindingResult bindWhitelist(
        const std::string& platform,
        const std::string& selfId,
        const std::string& userId,
        const std::string& channelId,
        const std::string& playerName,
        std::int64_t nowMs
    );
    std::optional<WhitelistBinding> findWhitelistBinding(
        const std::string& platform,
        const std::string& selfId,
        const std::string& userId
    ) const;
    std::optional<WhitelistBinding> unbindWhitelist(
        const std::string& platform,
        const std::string& selfId,
        const std::string& userId
    );
    std::pair<AdminWhitelistGrant, bool> addAdminWhitelist(
        const std::string& playerName,
        const std::string& addedBy,
        std::int64_t nowMs
    );
    bool hasWhitelistAuthorization(const std::string& playerName) const;
    bool revokePlayerWhitelist(const std::string& playerName);
    bool authorizePlayer(
        const std::string& playerName,
        const std::string& xuid,
        bool isOperator,
        bool operatorBypass
    );

private:
    PlayerRecord snapshotRecord(const PlayerRecord& record, std::int64_t nowMs) const;
    void markChanged();

    std::filesystem::path mFilePath;
    mutable std::mutex mSaveMutex;
    mutable std::mutex mMutex;
    std::unordered_map<std::string, PlayerRecord> mPlayers;
    std::unordered_map<std::string, std::int64_t> mActiveSessions;
    std::vector<WhitelistBinding> mBindings;
    std::vector<AdminWhitelistGrant> mAdminWhitelist;
    std::uint64_t mRevision = 0;
    bool mDirty = false;
    std::atomic<bool> mAvailable{true};
    std::atomic<bool> mRecoveredFromBackup{false};
};

} // namespace serverinfo_rest
