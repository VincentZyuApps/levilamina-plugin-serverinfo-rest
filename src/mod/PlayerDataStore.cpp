#include "mod/PlayerDataStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace serverinfo_rest {
namespace {

constexpr int DataVersion = 1;

struct DecodedData {
    std::unordered_map<std::string, PlayerRecord> players;
    std::vector<WhitelistBinding> bindings;
    std::vector<AdminWhitelistGrant> adminWhitelist;
};

std::string normalize(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string bindingKey(const std::string& platform, const std::string& selfId, const std::string& userId) {
    return normalize(platform) + "\n" + selfId + "\n" + userId;
}

std::int64_t readInt64(const nlohmann::json& json, const char* key, std::int64_t fallback = 0) {
    auto it = json.find(key);
    if (it == json.end() || !it->is_number_integer()) return fallback;
    return it->get<std::int64_t>();
}

std::uint64_t readUInt64(const nlohmann::json& json, const char* key, std::uint64_t fallback = 0) {
    auto value = readInt64(json, key, static_cast<std::int64_t>(fallback));
    return value < 0 ? fallback : static_cast<std::uint64_t>(value);
}

std::string readString(const nlohmann::json& json, const char* key) {
    auto it = json.find(key);
    return it != json.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool decodeData(const std::string& content, DecodedData& decoded, std::string& error) {
    auto json = nlohmann::json::parse(content, nullptr, false, true);
    if (json.is_discarded() || !json.is_object()) {
        error = "invalid JSON document";
        return false;
    }
    try {
        const auto version = json.find("version");
        if (version == json.end() || !version->is_number_integer() || version->get<int>() != DataVersion) {
            error = "unsupported or missing data version";
            return false;
        }

    const auto playersIt = json.find("players");
    const auto bindingsIt = json.find("bindings");
    const auto adminIt = json.find("adminWhitelist");
    if (playersIt == json.end() || !playersIt->is_array()
        || bindingsIt == json.end() || !bindingsIt->is_array()
        || adminIt == json.end() || !adminIt->is_array()) {
        error = "players, bindings and adminWhitelist must be arrays";
        return false;
    }

    DecodedData candidate;
    for (const auto& item : *playersIt) {
        if (!item.is_object()) {
            error = "players contains a non-object item";
            return false;
        }
        PlayerRecord record;
        record.xuid = readString(item, "xuid");
        if (record.xuid.empty()) {
            error = "player record is missing xuid";
            return false;
        }
        record.uuid = readString(item, "uuid");
        record.name = readString(item, "name");
        record.firstSeenMs = readInt64(item, "firstSeenMs");
        record.lastSeenMs = readInt64(item, "lastSeenMs");
        record.totalPlayMs = readInt64(item, "totalPlayMs");
        record.joinCount = readUInt64(item, "joinCount");
        record.blocksMined = readUInt64(item, "blocksMined");
        record.mobsKilled = readUInt64(item, "mobsKilled");
        candidate.players[record.xuid] = std::move(record);
    }

    for (const auto& item : *bindingsIt) {
        if (!item.is_object()) {
            error = "bindings contains a non-object item";
            return false;
        }
        WhitelistBinding binding;
        binding.platform = readString(item, "platform");
        binding.selfId = readString(item, "selfId");
        binding.userId = readString(item, "userId");
        binding.channelId = readString(item, "channelId");
        binding.playerName = readString(item, "playerName");
        binding.xuid = readString(item, "xuid");
        binding.boundAtMs = readInt64(item, "boundAtMs");
        if (binding.platform.empty() || binding.userId.empty() || binding.playerName.empty()) {
            error = "binding is missing platform, userId or playerName";
            return false;
        }
        candidate.bindings.push_back(std::move(binding));
    }

    for (const auto& item : *adminIt) {
        if (!item.is_object()) {
            error = "adminWhitelist contains a non-object item";
            return false;
        }
        AdminWhitelistGrant grant;
        grant.playerName = readString(item, "playerName");
        grant.xuid = readString(item, "xuid");
        grant.addedBy = readString(item, "addedBy");
        grant.addedAtMs = readInt64(item, "addedAtMs");
        if (grant.playerName.empty()) {
            error = "administrator allowlist grant is missing playerName";
            return false;
        }
        candidate.adminWhitelist.push_back(std::move(grant));
    }

        decoded = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        error = "invalid data value: " + std::string(exception.what());
        return false;
    }
}

bool readTextFile(const std::filesystem::path& path, std::string& content, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "failed to open " + path.string();
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (stream.bad()) {
        error = "failed to read " + path.string();
        return false;
    }
    content = buffer.str();
    return true;
}

bool writeTextFile(const std::filesystem::path& path, const std::string& content, std::string& error) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "failed to open " + path.string() + " for writing";
        return false;
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.flush();
    if (!stream) {
        error = "failed to write " + path.string();
        return false;
    }
    return true;
}

std::filesystem::path corruptPathFor(const std::filesystem::path& path) {
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return path.parent_path() / (path.filename().string() + ".corrupt-" + std::to_string(timestamp) + ".json");
}

std::filesystem::path preserveCorruptFile(const std::filesystem::path& path, std::string& warning) {
    if (!std::filesystem::exists(path)) return {};
    auto destination = corruptPathFor(path);
    std::error_code ec;
    std::filesystem::rename(path, destination, ec);
    if (ec) {
        warning = "failed to preserve corrupt file " + path.string() + ": " + ec.message();
        return {};
    }
    return destination;
}

bool replaceFileAtomically(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    const std::filesystem::path& backup,
    std::string& error
) {
#ifdef _WIN32
    if (std::filesystem::exists(destination)) {
        const auto nextBackup = std::filesystem::path(backup.string() + ".tmp");
        std::error_code cleanupError;
        std::filesystem::remove(nextBackup, cleanupError);
        if (::ReplaceFileW(
                destination.c_str(),
                temporary.c_str(),
                nextBackup.c_str(),
                REPLACEFILE_WRITE_THROUGH,
                nullptr,
                nullptr
            ) != 0) {
            if (::MoveFileExW(
                    nextBackup.c_str(),
                    backup.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
                ) == 0) {
                std::error_code failedBackupCleanupError;
                std::filesystem::remove(nextBackup, failedBackupCleanupError);
            }
            return true;
        }
        error = "ReplaceFileW failed with error " + std::to_string(::GetLastError());
        return false;
    }
    if (::MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ) != 0) {
        return true;
    }
    error = "MoveFileExW failed with error " + std::to_string(::GetLastError());
    return false;
#else
    std::error_code ec;
    if (std::filesystem::exists(destination)) {
        std::filesystem::copy_file(destination, backup, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "failed to create backup: " + ec.message();
            return false;
        }
    }
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        error = "failed to replace data file: " + ec.message();
        return false;
    }
    return true;
#endif
}

} // namespace

PlayerDataStore::PlayerDataStore(std::filesystem::path filePath) : mFilePath(std::move(filePath)) {}

bool PlayerDataStore::load(std::string& error) {
    std::lock_guard<std::mutex> saveLock(mSaveMutex);
    mRecoveredFromBackup = false;

    auto applyDecoded = [this](DecodedData&& decoded) {
        std::lock_guard<std::mutex> lock(mMutex);
        mPlayers = std::move(decoded.players);
        mBindings = std::move(decoded.bindings);
        mAdminWhitelist = std::move(decoded.adminWhitelist);
        mActiveSessions.clear();
        mDirty = false;
        mRevision = 0;
    };
    auto tryLoad = [](const std::filesystem::path& path, DecodedData& decoded, std::string& reason) {
        std::string content;
        if (!readTextFile(path, content, reason)) return false;
        if (!decodeData(content, decoded, reason)) {
            reason = path.string() + ": " + reason;
            return false;
        }
        return true;
    };

    const auto backup = backupPath();
    if (!std::filesystem::exists(mFilePath) && !std::filesystem::exists(backup)) {
        applyDecoded({});
        mAvailable = true;
        return true;
    }

    std::string primaryReason;
    if (std::filesystem::exists(mFilePath)) {
        DecodedData decoded;
        if (tryLoad(mFilePath, decoded, primaryReason)) {
            applyDecoded(std::move(decoded));
            mAvailable = true;
            return true;
        }
        std::string preserveWarning;
        const auto corrupt = preserveCorruptFile(mFilePath, preserveWarning);
        if (!corrupt.empty()) primaryReason += "; preserved as " + corrupt.string();
        if (!preserveWarning.empty()) primaryReason += "; " + preserveWarning;
    } else {
        primaryReason = "primary data file is missing";
    }

    std::string backupReason;
    if (std::filesystem::exists(backup)) {
        DecodedData decoded;
        if (tryLoad(backup, decoded, backupReason)) {
            applyDecoded(std::move(decoded));
            std::error_code directoryError;
            std::filesystem::create_directories(mFilePath.parent_path(), directoryError);
            const auto temporary = std::filesystem::path(mFilePath.string() + ".tmp");
            std::error_code ec;
            std::filesystem::copy_file(backup, temporary, std::filesystem::copy_options::overwrite_existing, ec);
            std::string restoreError;
            if (ec || !replaceFileAtomically(temporary, mFilePath, backup, restoreError)) {
                error = "loaded backup, but failed to restore primary file: "
                    + (ec ? ec.message() : restoreError) + "; primary error: " + primaryReason;
            } else {
                error = "recovered player data from " + backup.string() + "; primary error: " + primaryReason;
            }
            mRecoveredFromBackup = true;
            mAvailable = true;
            return true;
        }
        std::string preserveWarning;
        const auto corrupt = preserveCorruptFile(backup, preserveWarning);
        if (!corrupt.empty()) backupReason += "; preserved as " + corrupt.string();
        if (!preserveWarning.empty()) backupReason += "; " + preserveWarning;
    } else {
        backupReason = "backup data file is missing";
    }

    mAvailable = false;
    error = "player data unavailable; primary: " + primaryReason + "; backup: " + backupReason;
    return false;
}

bool PlayerDataStore::save(std::string& error, bool force) {
    std::lock_guard<std::mutex> saveLock(mSaveMutex);
    if (!mAvailable) {
        error = "player data store is unavailable; refusing to overwrite damaged data";
        return false;
    }
    nlohmann::json json;
    std::uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!force && !mDirty) return true;

        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        json["version"] = DataVersion;
        json["players"] = nlohmann::json::array();
        for (const auto& [_, record] : mPlayers) {
            auto snapshot = snapshotRecord(record, nowMs);
            json["players"].push_back({
                {"xuid", snapshot.xuid},
                {"uuid", snapshot.uuid},
                {"name", snapshot.name},
                {"firstSeenMs", snapshot.firstSeenMs},
                {"lastSeenMs", snapshot.lastSeenMs},
                {"totalPlayMs", snapshot.totalPlayMs},
                {"joinCount", snapshot.joinCount},
                {"blocksMined", snapshot.blocksMined},
                {"mobsKilled", snapshot.mobsKilled}
            });
        }

        json["bindings"] = nlohmann::json::array();
        for (const auto& binding : mBindings) {
            json["bindings"].push_back({
                {"platform", binding.platform},
                {"selfId", binding.selfId},
                {"userId", binding.userId},
                {"channelId", binding.channelId},
                {"playerName", binding.playerName},
                {"xuid", binding.xuid},
                {"boundAtMs", binding.boundAtMs}
            });
        }
        json["adminWhitelist"] = nlohmann::json::array();
        for (const auto& grant : mAdminWhitelist) {
            json["adminWhitelist"].push_back({
                {"playerName", grant.playerName},
                {"xuid", grant.xuid},
                {"addedBy", grant.addedBy},
                {"addedAtMs", grant.addedAtMs}
            });
        }
        revision = mRevision;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(mFilePath.parent_path(), directoryError);
    if (directoryError) {
        error = "failed to create player data directory: " + directoryError.message();
        return false;
    }
    const auto temporary = std::filesystem::path(mFilePath.string() + ".tmp");
    const auto serialized = json.dump(2);
    if (!writeTextFile(temporary, serialized, error)) return false;

    std::string written;
    DecodedData verification;
    if (!readTextFile(temporary, written, error) || !decodeData(written, verification, error)) {
        error = "temporary player data validation failed: " + error;
        return false;
    }
    if (!replaceFileAtomically(temporary, mFilePath, backupPath(), error)) return false;

    std::lock_guard<std::mutex> lock(mMutex);
    if (mRevision == revision) mDirty = false;
    return true;
}

std::filesystem::path PlayerDataStore::backupPath() const {
    return std::filesystem::path(mFilePath.string() + ".bak");
}

std::vector<std::string> PlayerDataStore::authorizedPlayerNames() const {
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    result.reserve(mBindings.size() + mAdminWhitelist.size());
    auto append = [&](const std::string& name) {
        if (!name.empty() && seen.insert(normalize(name)).second) result.push_back(name);
    };
    for (const auto& binding : mBindings) append(binding.playerName);
    for (const auto& grant : mAdminWhitelist) append(grant.playerName);
    return result;
}

void PlayerDataStore::playerJoined(
    const std::string& xuid,
    const std::string& uuid,
    const std::string& name,
    std::int64_t nowMs
) {
    if (xuid.empty()) return;
    std::lock_guard<std::mutex> lock(mMutex);
    auto& record = mPlayers[xuid];
    record.xuid = xuid;
    record.uuid = uuid;
    record.name = name;
    if (record.firstSeenMs <= 0) record.firstSeenMs = nowMs;
    record.lastSeenMs = nowMs;
    ++record.joinCount;
    mActiveSessions[xuid] = nowMs;
    markChanged();
}

void PlayerDataStore::playerLeft(const std::string& xuid, std::int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto record = mPlayers.find(xuid);
    if (record == mPlayers.end()) return;
    auto session = mActiveSessions.find(xuid);
    if (session != mActiveSessions.end()) {
        record->second.totalPlayMs += std::max<std::int64_t>(0, nowMs - session->second);
        mActiveSessions.erase(session);
    }
    record->second.lastSeenMs = nowMs;
    markChanged();
}

void PlayerDataStore::incrementBlocksMined(const std::string& xuid, const std::string& name, std::int64_t nowMs) {
    if (xuid.empty()) return;
    std::lock_guard<std::mutex> lock(mMutex);
    auto& record = mPlayers[xuid];
    record.xuid = xuid;
    record.name = name;
    if (record.firstSeenMs <= 0) record.firstSeenMs = nowMs;
    record.lastSeenMs = nowMs;
    ++record.blocksMined;
    markChanged();
}

void PlayerDataStore::incrementMobsKilled(const std::string& xuid, const std::string& name, std::int64_t nowMs) {
    if (xuid.empty()) return;
    std::lock_guard<std::mutex> lock(mMutex);
    auto& record = mPlayers[xuid];
    record.xuid = xuid;
    record.name = name;
    if (record.firstSeenMs <= 0) record.firstSeenMs = nowMs;
    record.lastSeenMs = nowMs;
    ++record.mobsKilled;
    markChanged();
}

PlayerHistoryPage PlayerDataStore::history(int page, int pageSize, std::int64_t nowMs) const {
    page = std::max(1, page);
    pageSize = std::clamp(pageSize, 1, 100);

    std::vector<PlayerRecord> records;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        records.reserve(mPlayers.size());
        for (const auto& [_, record] : mPlayers) records.push_back(snapshotRecord(record, nowMs));
    }
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        if (left.lastSeenMs != right.lastSeenMs) return left.lastSeenMs > right.lastSeenMs;
        return normalize(left.name) < normalize(right.name);
    });

    PlayerHistoryPage result;
    result.total = records.size();
    result.pageSize = pageSize;
    const auto pageCount = std::max<std::size_t>(
        1,
        (records.size() + static_cast<std::size_t>(pageSize) - 1) / static_cast<std::size_t>(pageSize)
    );
    page = std::min(page, static_cast<int>(std::min<std::size_t>(pageCount, std::numeric_limits<int>::max())));
    result.page = page;
    const auto offset = static_cast<std::size_t>(page - 1) * static_cast<std::size_t>(pageSize);
    if (offset < records.size()) {
        const auto end = std::min(records.size(), offset + static_cast<std::size_t>(pageSize));
        result.players.assign(records.begin() + static_cast<std::ptrdiff_t>(offset), records.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return result;
}

std::optional<PlayerRecord> PlayerDataStore::findPlayer(const std::string& nameOrXuid, std::int64_t nowMs) const {
    std::lock_guard<std::mutex> lock(mMutex);
    auto exact = mPlayers.find(nameOrXuid);
    if (exact != mPlayers.end()) return snapshotRecord(exact->second, nowMs);

    const auto wanted = normalize(nameOrXuid);
    for (const auto& [_, record] : mPlayers) {
        if (normalize(record.name) == wanted) return snapshotRecord(record, nowMs);
    }
    return std::nullopt;
}

BindingResult PlayerDataStore::bindWhitelist(
    const std::string& platform,
    const std::string& selfId,
    const std::string& userId,
    const std::string& channelId,
    const std::string& playerName,
    std::int64_t nowMs
) {
    BindingResult result;
    const auto wantedUser = bindingKey(platform, selfId, userId);
    const auto wantedPlayer = normalize(playerName);
    std::lock_guard<std::mutex> lock(mMutex);

    for (const auto& binding : mBindings) {
        if (bindingKey(binding.platform, binding.selfId, binding.userId) == wantedUser) {
            if (normalize(binding.playerName) == wantedPlayer) {
                result.success = true;
                result.binding = binding;
            } else {
                result.error = "this chat account is already bound to another player";
            }
            return result;
        }
        if (normalize(binding.playerName) == wantedPlayer) {
            result.error = "this player is already bound to another chat account";
            return result;
        }
    }

    WhitelistBinding binding;
    binding.platform = normalize(platform);
    binding.selfId = selfId;
    binding.userId = userId;
    binding.channelId = channelId;
    binding.playerName = playerName;
    binding.boundAtMs = nowMs;
    mBindings.push_back(binding);
    markChanged();
    result.success = true;
    result.created = true;
    result.binding = std::move(binding);
    return result;
}

std::optional<WhitelistBinding> PlayerDataStore::unbindWhitelist(
    const std::string& platform,
    const std::string& selfId,
    const std::string& userId
) {
    const auto wanted = bindingKey(platform, selfId, userId);
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = std::find_if(mBindings.begin(), mBindings.end(), [&](const auto& binding) {
        return bindingKey(binding.platform, binding.selfId, binding.userId) == wanted;
    });
    if (it == mBindings.end()) return std::nullopt;
    auto binding = *it;
    mBindings.erase(it);
    markChanged();
    return binding;
}

std::pair<AdminWhitelistGrant, bool> PlayerDataStore::addAdminWhitelist(
    const std::string& playerName,
    const std::string& addedBy,
    std::int64_t nowMs
) {
    const auto wanted = normalize(playerName);
    std::lock_guard<std::mutex> lock(mMutex);
    for (const auto& grant : mAdminWhitelist) {
        if (normalize(grant.playerName) == wanted) return {grant, false};
    }
    AdminWhitelistGrant grant;
    grant.playerName = playerName;
    grant.addedBy = addedBy;
    grant.addedAtMs = nowMs;
    mAdminWhitelist.push_back(grant);
    markChanged();
    return {grant, true};
}

bool PlayerDataStore::hasWhitelistAuthorization(const std::string& playerName) const {
    const auto wanted = normalize(playerName);
    std::lock_guard<std::mutex> lock(mMutex);
    return std::ranges::any_of(mAdminWhitelist, [&](const auto& grant) {
        return normalize(grant.playerName) == wanted;
    }) || std::ranges::any_of(mBindings, [&](const auto& binding) {
        return normalize(binding.playerName) == wanted;
    });
}

bool PlayerDataStore::revokePlayerWhitelist(const std::string& playerName) {
    const auto wanted = normalize(playerName);
    std::lock_guard<std::mutex> lock(mMutex);
    const auto previousGrantCount = mAdminWhitelist.size();
    const auto previousBindingCount = mBindings.size();
    std::erase_if(mAdminWhitelist, [&](const auto& grant) {
        return normalize(grant.playerName) == wanted;
    });
    std::erase_if(mBindings, [&](const auto& binding) {
        return normalize(binding.playerName) == wanted;
    });
    if (previousGrantCount == mAdminWhitelist.size() && previousBindingCount == mBindings.size()) return false;
    markChanged();
    return true;
}

bool PlayerDataStore::authorizePlayer(
    const std::string& playerName,
    const std::string& xuid,
    bool isOperator,
    bool operatorBypass
) {
    if (isOperator && operatorBypass) return true;
    const auto wantedName = normalize(playerName);
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto& grant : mAdminWhitelist) {
        if (!grant.xuid.empty() && grant.xuid == xuid) {
            if (grant.playerName != playerName) {
                grant.playerName = playerName;
                markChanged();
            }
            return true;
        }
        if (grant.xuid.empty() && normalize(grant.playerName) == wantedName) {
            grant.xuid = xuid;
            grant.playerName = playerName;
            markChanged();
            return true;
        }
    }
    for (auto& binding : mBindings) {
        if (!binding.xuid.empty() && binding.xuid == xuid) {
            if (binding.playerName != playerName) {
                binding.playerName = playerName;
                markChanged();
            }
            return true;
        }
        if (binding.xuid.empty() && normalize(binding.playerName) == wantedName) {
            binding.xuid = xuid;
            binding.playerName = playerName;
            markChanged();
            return true;
        }
    }
    return false;
}

PlayerRecord PlayerDataStore::snapshotRecord(const PlayerRecord& record, std::int64_t nowMs) const {
    auto snapshot = record;
    auto session = mActiveSessions.find(record.xuid);
    if (session != mActiveSessions.end()) {
        snapshot.totalPlayMs += std::max<std::int64_t>(0, nowMs - session->second);
        snapshot.lastSeenMs = std::max(snapshot.lastSeenMs, nowMs);
    }
    return snapshot;
}

void PlayerDataStore::markChanged() {
    mDirty = true;
    ++mRevision;
}

} // namespace serverinfo_rest
