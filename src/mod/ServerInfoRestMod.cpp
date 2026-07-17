#include "mod/ServerInfoRestMod.h"
#include "mod/ExecCommandNative.h"
#include "mod/HttpServer.h"
#include "mod/PlayerDataStore.h"

#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/Config.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/Versions.h"
#include "ll/api/io/LogLevel.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerConnectEvent.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/PropertiesSettings.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <string_view>

namespace serverinfo_rest {

namespace {
constexpr auto PluginVersion = "0.1.0";

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::string urlDecode(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            result.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            auto high = hexValue(value[i + 1]);
            auto low = hexValue(value[i + 2]);
            if (high >= 0 && low >= 0) {
                result.push_back(static_cast<char>((high << 4) | low));
                i += 2;
            } else {
                result.push_back(value[i]);
            }
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::string getQueryParam(const std::string& query, const std::string& wantedKey) {
    std::istringstream stream(query);
    std::string param;
    while (std::getline(stream, param, '&')) {
        auto pos = param.find('=');
        if (pos == std::string::npos) continue;
        if (urlDecode(std::string_view(param).substr(0, pos)) == wantedKey) {
            return urlDecode(std::string_view(param).substr(pos + 1));
        }
    }
    return {};
}

std::string getHeaderCaseInsensitive(const HttpRequest& request, std::string wantedKey) {
    std::transform(wantedKey.begin(), wantedKey.end(), wantedKey.begin(), [](unsigned char ch) { return std::tolower(ch); });
    for (const auto& [key, value] : request.headers) {
        auto normalized = key;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return std::tolower(ch); });
        if (normalized == wantedKey) return value;
    }
    return {};
}

double roundTps(double value) {
    return std::round(value * 100.0) / 100.0;
}

long long unixTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string getBearerToken(const HttpRequest& request, bool allowQueryToken) {
    auto authorization = getHeaderCaseInsensitive(request, "authorization");
    constexpr std::string_view prefix = "Bearer ";
    if (authorization.size() >= prefix.size()
        && std::equal(prefix.begin(), prefix.end(), authorization.begin(), [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
        })) {
        return authorization.substr(prefix.size());
    }
    return allowQueryToken ? getQueryParam(request.query, "token") : std::string{};
}

bool secureEquals(std::string_view left, std::string_view right) {
    std::size_t difference = left.size() ^ right.size();
    const auto count = std::max(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto lhs = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        const auto rhs = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        difference |= lhs ^ rhs;
    }
    return difference == 0;
}

int parseBoundedInt(const std::string& value, int fallback, int minimum, int maximum) {
    if (value.empty()) return fallback;
    try {
        auto parsed = std::stoll(value);
        return static_cast<int>(std::clamp<long long>(parsed, minimum, maximum));
    } catch (...) {
        return fallback;
    }
}

std::string trimCommand(std::string command) {
    auto first = command.find_first_not_of(" \t/\r\n");
    if (first == std::string::npos) return {};
    command.erase(0, first);
    auto last = command.find_last_not_of(" \t\r\n");
    command.resize(last + 1);
    return command;
}

bool commandAllowed(const std::string& command, const std::vector<std::string>& prefixes) {
    if (prefixes.empty()) return true;
    auto normalized = command;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    for (auto prefix : prefixes) {
        prefix = trimCommand(std::move(prefix));
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (prefix.empty()) continue;
        if (normalized == prefix) return true;
        if (normalized.starts_with(prefix) && normalized.size() > prefix.size()
            && std::isspace(static_cast<unsigned char>(normalized[prefix.size()]))) {
            return true;
        }
    }
    return false;
}

bool validPlayerName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char ch) {
        return ch < 0x20 || ch == '"' || ch == '\\';
    });
}

std::string truncateUtf8(std::string text, std::size_t limit) {
    if (text.size() <= limit) return text;
    text.resize(limit);
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
    text += "\n...[output truncated]";
    return text;
}

nlohmann::json playerRecordJson(const PlayerRecord& player) {
    return {
        {"xuid", player.xuid},
        {"uuid", player.uuid},
        {"name", player.name},
        {"firstSeenMs", player.firstSeenMs},
        {"lastSeenMs", player.lastSeenMs},
        {"totalPlayMs", player.totalPlayMs},
        {"joinCount", player.joinCount},
        {"blocksMined", player.blocksMined},
        {"mobsKilled", player.mobsKilled},
        {"money", nullptr},
        {"moneyAvailable", false}
    };
}

nlohmann::json bindingJson(const WhitelistBinding& binding) {
    return {
        {"platform", binding.platform},
        {"selfId", binding.selfId},
        {"userId", binding.userId},
        {"channelId", binding.channelId},
        {"playerName", binding.playerName},
        {"xuid", binding.xuid},
        {"boundAtMs", binding.boundAtMs}
    };
}
} // namespace

// 将字符串转换为日志级别
static ll::io::LogLevel parseLogLevel(const std::string& levelStr) {
    std::string lower = levelStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), 
                   [](unsigned char c){ return std::tolower(c); });
    
    if (lower == "silent" || lower == "off") return ll::io::LogLevel::Off;
    if (lower == "fatal") return ll::io::LogLevel::Fatal;
    if (lower == "error") return ll::io::LogLevel::Error;
    if (lower == "warn" || lower == "warning") return ll::io::LogLevel::Warn;
    if (lower == "info") return ll::io::LogLevel::Info;
    if (lower == "debug") return ll::io::LogLevel::Debug;
    if (lower == "trace") return ll::io::LogLevel::Trace;
    
    return ll::io::LogLevel::Info;
}

ServerInfoRestMod::ServerInfoRestMod() : mSelf(*ll::mod::NativeMod::current()) {}

ServerInfoRestMod::~ServerInfoRestMod() = default;

ServerInfoRestMod& ServerInfoRestMod::getInstance() {
    static ServerInfoRestMod instance;
    return instance;
}

// ==================== 玩家缓存方法实现 ====================

std::vector<CachedPlayerInfo> ServerInfoRestMod::getPlayerCache() const {
    std::lock_guard<std::mutex> lock(mPlayerCacheMutex);
    std::vector<CachedPlayerInfo> result;
    result.reserve(mPlayerCache.size());
    for (const auto& [xuid, info] : mPlayerCache) {
        result.push_back(info);
    }
    getSelf().getLogger().trace("getPlayerCache() called, returning {} players", result.size());
    return result;
}

std::optional<CachedPlayerInfo> ServerInfoRestMod::getPlayerByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mPlayerCacheMutex);
    getSelf().getLogger().trace("getPlayerByName() called for: {}", name);
    for (const auto& [xuid, info] : mPlayerCache) {
        if (info.name == name) {
            getSelf().getLogger().trace("Found player {} in cache (xuid: {})", name, xuid);
            return info;
        }
    }
    getSelf().getLogger().trace("Player {} not found in cache", name);
    return std::nullopt;
}

int ServerInfoRestMod::getPlayerCount() const {
    std::lock_guard<std::mutex> lock(mPlayerCacheMutex);
    int count = static_cast<int>(mPlayerCache.size());
    getSelf().getLogger().trace("getPlayerCount() = {}", count);
    return count;
}

TpsSnapshot ServerInfoRestMod::getTpsSnapshot() const {
    std::lock_guard<std::mutex> lock(mTpsMutex);
    TpsSnapshot snapshot;
    snapshot.sampledSeconds = static_cast<int>(mTpsSamples.size());
    if (mTpsSamples.empty()) return snapshot;

    auto average = [this](size_t window) {
        auto count = std::min(window, mTpsSamples.size());
        double sum = 0.0;
        for (auto it = mTpsSamples.end() - static_cast<std::ptrdiff_t>(count); it != mTpsSamples.end(); ++it) {
            sum += *it;
        }
        return count == 0 ? 0.0 : sum / static_cast<double>(count);
    };

    snapshot.realtime = roundTps(mTpsSamples.back());
    snapshot.avg10s = roundTps(average(10));
    snapshot.avg60s = roundTps(average(60));
    snapshot.avg300s = roundTps(average(300));
    return snapshot;
}

int ServerInfoRestMod::getMaxPlayers() const {
    auto properties = ll::service::getPropertiesSettings();
    return properties ? static_cast<int>(properties->mMaxPlayers) : 0;
}

long long ServerInfoRestMod::getUptimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - mStartedAt
    ).count();
}

void ServerInfoRestMod::onServerTick() {
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mTpsMutex);
        ++mTicksInCurrentBucket;
        auto elapsed = std::chrono::duration<double>(now - mTpsBucketStartedAt).count();
        if (elapsed >= 1.0) {
            auto tps = std::clamp(static_cast<double>(mTicksInCurrentBucket) / elapsed, 0.0, 20.0);
            auto elapsedSeconds = std::max(1, static_cast<int>(std::floor(elapsed)));
            for (int i = 0; i < elapsedSeconds; ++i) mTpsSamples.push_back(tps);
            while (mTpsSamples.size() > 300) mTpsSamples.pop_front();
            mTicksInCurrentBucket = 0;
            mTpsBucketStartedAt = now;
        }
    }

    const auto saveInterval = std::chrono::seconds(std::clamp(mConfig.dataSaveIntervalSeconds, 5, 3600));
    if (now - mLastDataSaveAt >= saveInterval) {
        savePlayerData();
        mLastDataSaveAt = now;
    }
}

void ServerInfoRestMod::onPlayerJoin(const std::string& xuid, const CachedPlayerInfo& info) {
    if (mPlayerDataStore) mPlayerDataStore->playerJoined(xuid, info.uuid, info.name, unixTimeMs());
    std::lock_guard<std::mutex> lock(mPlayerCacheMutex);
    mPlayerCache[xuid] = info;
    getSelf().getLogger().info("[Cache] Player joined: {} (xuid: {})", info.name, xuid);
    getSelf().getLogger().debug("[Cache] Player details - uuid: {}, ip: {}, locale: {}, op: {}", 
                                 info.uuid, info.ipAndPort, info.locale, info.isOperator);
    getSelf().getLogger().trace("[Cache] Player position: ({:.2f}, {:.2f}, {:.2f})", 
                                 info.posX, info.posY, info.posZ);
    getSelf().getLogger().debug("[Cache] Total players in cache: {}", mPlayerCache.size());
}

void ServerInfoRestMod::onPlayerLeave(const std::string& xuid) {
    if (mPlayerDataStore) mPlayerDataStore->playerLeft(xuid, unixTimeMs());
    std::lock_guard<std::mutex> lock(mPlayerCacheMutex);
    auto it = mPlayerCache.find(xuid);
    if (it != mPlayerCache.end()) {
        std::string name = it->second.name;
        mPlayerCache.erase(it);
        getSelf().getLogger().info("[Cache] Player left: {} (xuid: {})", name, xuid);
        getSelf().getLogger().debug("[Cache] Total players in cache: {}", mPlayerCache.size());
    } else {
        getSelf().getLogger().warn("[Cache] Tried to remove unknown player with xuid: {}", xuid);
    }
}

void ServerInfoRestMod::savePlayerData(bool force) {
    if (!mPlayerDataStore) return;
    std::string error;
    if (!mPlayerDataStore->save(error, force)) {
        getSelf().getLogger().error("[Data] {}", error);
    }
}

// ==================== 生命周期方法 ====================

bool ServerInfoRestMod::load() {
    auto& logger = getSelf().getLogger();
    
    // ASCII Art Banner
    logger.info("");
    logger.info(R"(                                   _       ____                           __)");
    logger.info(R"(   ________  ______   _____  _____(_)___  / __/___        ________  _____/ /_)");
    logger.info(R"(  / ___/ _ \/ ___/ | / / _ \/ ___/ / __ \/ /_/ __ \______/ ___/ _ \/ ___/ __/)");
    logger.info(R"( (__  )  __/ /   | |/ /  __/ /  / / / / / __/ /_/ /_____/ /  /  __(__  ) /_  )");
    logger.info(R"(/____/\___/_/    |___/\___/_/  /_/_/ /_/_/  \____/     /_/   \___/____/\__/  )");
    logger.info("");
    logger.info("  Author: VincentZyu");
    logger.info("  GitHub Profile: https://github.com/VincentZyu233");
    logger.info("  GitHub Repo: https://github.com/VincentZyu233/levilamina-plugin-serverinfo-rest");
    logger.info("");

    // 读取配置文件
    const auto& configFilePath = getSelf().getConfigDir() / "config.json";
    if (!ll::config::loadConfig(mConfig, configFilePath)) {
        logger.warn("Cannot load configurations from {}", configFilePath.string());
        logger.info("Saving default configurations...");
        if (!ll::config::saveConfig(mConfig, configFilePath)) {
            logger.error("Failed to save default configurations!");
        }
    }

    mPlayerDataStore = std::make_unique<PlayerDataStore>(getSelf().getDataDir() / "player-data.json");
    std::string playerDataError;
    if (!mPlayerDataStore->load(playerDataError)) {
        logger.error("Failed to load player history: {}", playerDataError);
    }

    // 设置日志级别
    ll::io::LogLevel logLevel = parseLogLevel(mConfig.logLevel);
    logger.setLevel(logLevel);
    logger.info("Log level set to: {}", mConfig.logLevel);

    // 输出配置信息
    logger.debug("Configuration loaded:");
    logger.debug("  - host: {}", mConfig.host);
    logger.debug("  - port: {}", mConfig.port);
    logger.debug("  - enableCors: {}", mConfig.enableCors);
    logger.debug("  - apiPrefix: {}", mConfig.apiPrefix);
    logger.debug("  - enableToken: {}", mConfig.enableToken);
    logger.debug("  - enableCommandExecution: {}", mConfig.enableCommandExecution);
    logger.debug("  - enableWhitelistBinding: {}", mConfig.enableWhitelistBinding);
    logger.debug("  - enforceWhitelistBinding: {}", mConfig.enforceWhitelistBinding);
    if (mConfig.enableToken) {
        logger.info("Token authentication is ENABLED");
        if (mConfig.token.empty()) {
            logger.warn("Token is empty! Please set a token in config.json");
        }
    }

    logger.info("serverinfo-rest loaded successfully!");
    return true;
}

bool ServerInfoRestMod::enable() {
    auto& logger = getSelf().getLogger();
    logger.info("Enabling serverinfo-rest...");

    // ==================== 注册玩家事件监听器 ====================
    auto& eventBus = ll::event::EventBus::getInstance();

    mStartedAt = std::chrono::steady_clock::now();
    mLastDataSaveAt = mStartedAt;

    mPlayerConnectListener = eventBus.emplaceListener<ll::event::player::PlayerConnectEvent>(
        [this](ll::event::player::PlayerConnectEvent& event) {
            if (!mConfig.enforceWhitelistBinding || !mPlayerDataStore) return;
            auto& player = event.self();
            if (mPlayerDataStore->authorizePlayer(
                    player.getRealName(),
                    player.getXuid(),
                    player.isOperator(),
                    mConfig.operatorBypassBinding
                )) {
                return;
            }
            getSelf().getLogger().info(
                "[Whitelist] Rejected unbound player {} ({})",
                player.getRealName(),
                player.getXuid()
            );
            player.Player::disconnect("请先在群内使用绑定白名单指令绑定 Xbox 玩家名");
            event.cancel();
        },
        ll::event::EventPriority::Highest
    );

    {
        std::lock_guard<std::mutex> lock(mTpsMutex);
        mTpsSamples.clear();
        mTicksInCurrentBucket = 0;
        mTpsBucketStartedAt = mStartedAt;
    }
    mServerTickListener = eventBus.emplaceListener<ll::event::ServerLevelTickEvent>(
        [this](ll::event::ServerLevelTickEvent&) { onServerTick(); }
    );
    logger.info("ServerLevelTickEvent listener registered successfully");

    // 玩家加入事件
    logger.debug("Registering PlayerJoinEvent listener...");
    mPlayerJoinListener = eventBus.emplaceListener<ll::event::player::PlayerJoinEvent>(
        [this](ll::event::player::PlayerJoinEvent& event) {
            getSelf().getLogger().trace("[Event] PlayerJoinEvent triggered");
            auto& player = event.self();
            CachedPlayerInfo info;
            info.name = player.getRealName();
            info.xuid = player.getXuid();
            info.uuid = player.getUuid().asString();
            info.ipAndPort = player.getIPAndPort();
            info.locale = player.getLocaleCode();
            info.isOperator = player.isOperator();
            auto pos = player.getPosition();
            info.posX = pos.x;
            info.posY = pos.y;
            info.posZ = pos.z;
            
            getSelf().getLogger().trace("[Event] Extracted player info for: {}", info.name);
            onPlayerJoin(info.xuid, info);
        }
    );
    logger.info("PlayerJoinEvent listener registered successfully");

    // 玩家离开事件
    logger.debug("Registering PlayerDisconnectEvent listener...");
    mPlayerLeaveListener = eventBus.emplaceListener<ll::event::player::PlayerDisconnectEvent>(
        [this](ll::event::player::PlayerDisconnectEvent& event) {
            getSelf().getLogger().trace("[Event] PlayerDisconnectEvent triggered");
            auto& player = event.self();
            getSelf().getLogger().trace("[Event] Player disconnecting: {}", player.getRealName());
            onPlayerLeave(player.getXuid());
        }
    );
    logger.info("PlayerDisconnectEvent listener registered successfully");

    mPlayerDestroyBlockListener = eventBus.emplaceListener<ll::event::player::PlayerDestroyBlockEvent>(
        [this](ll::event::player::PlayerDestroyBlockEvent& event) {
            if (event.isCancelled() || !mPlayerDataStore) return;
            auto& player = event.self();
            mPlayerDataStore->incrementBlocksMined(player.getXuid(), player.getRealName(), unixTimeMs());
        },
        ll::event::EventPriority::Lowest
    );
    logger.info("PlayerDestroyBlockEvent listener registered successfully");

    mMobDieListener = eventBus.emplaceListener<ll::event::entity::MobDieEvent>(
        [this](ll::event::entity::MobDieEvent& event) {
            if (!mPlayerDataStore || event.self().isPlayer() || !event.source().isEntitySource()) return;
            auto* player = event.self().getLastHurtByPlayer();
            if (!player) return;
            mPlayerDataStore->incrementMobsKilled(player->getXuid(), player->getRealName(), unixTimeMs());
        },
        ll::event::EventPriority::Lowest
    );
    logger.info("MobDieEvent listener registered successfully");

    // ==================== 创建 HTTP 服务器 ====================
    mHttpServer = std::make_unique<HttpServer>(mConfig.host, mConfig.port, this);
    
    if (!mHttpServer->start()) {
        logger.error("Failed to start HTTP server!");
        return false;
    }

    std::string prefix = mConfig.apiPrefix;

    // Token 验证辅助函数
    auto validateToken = [this](const HttpRequest& req, HttpResponse& res) -> bool {
        if (!mConfig.enableToken) return true;
        auto reqToken = getBearerToken(req, true);
        if (reqToken.empty()) {
            res.setStatus(401, "Unauthorized");
            res.setJson("{\"error\": \"Missing access token\"}");
            getSelf().getLogger().debug("Request rejected: missing token");
            return false;
        }
        if (!secureEquals(reqToken, mConfig.token)) {
            res.setStatus(403, "Forbidden");
            res.setJson("{\"error\": \"Invalid token\"}");
            getSelf().getLogger().debug("Request rejected: invalid token");
            return false;
        }
        
        return true;
    };

    auto validateAdminToken = [this](const HttpRequest& req, HttpResponse& res) -> bool {
        if (mConfig.adminToken.empty()) {
            res.setStatus(503, "Service Unavailable");
            res.setJson("{\"error\": \"Admin API token is not configured\"}");
            return false;
        }
        auto reqToken = getBearerToken(req, false);
        if (reqToken.empty()) {
            res.setStatus(401, "Unauthorized");
            res.setJson("{\"error\": \"Missing admin access token\"}");
            return false;
        }
        if (!secureEquals(reqToken, mConfig.adminToken)) {
            res.setStatus(403, "Forbidden");
            res.setJson("{\"error\": \"Invalid admin token\"}");
            return false;
        }
        return true;
    };

    auto parseJsonBody = [](const HttpRequest& req, HttpResponse& res) -> std::optional<nlohmann::json> {
        if (req.body.empty()) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"JSON request body is required\"}");
            return std::nullopt;
        }
        auto json = nlohmann::json::parse(req.body, nullptr, false, true);
        if (json.is_discarded() || !json.is_object()) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"Invalid JSON request body\"}");
            return std::nullopt;
        }
        return json;
    };

    // ==================== 注册 API 路由 ====================

    // GET /api/v1/status - 服务器状态
    mHttpServer->get(prefix + "/status", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        getSelf().getLogger().trace("[API] /status endpoint called");
        if (!validateToken(req, res)) return;
        
        nlohmann::json json;
        json["status"] = "online";
        json["plugin"] = "serverinfo-rest";
        json["version"] = PluginVersion;
        json["playerCount"] = getPlayerCount();
        json["bdsVersion"] = ll::getGameVersion().to_string();
        json["protocolVersion"] = ll::getNetworkProtocolVersion();
        
        getSelf().getLogger().debug("[API] /status response: playerCount={}", json["playerCount"].get<int>());
        res.setJson(json.dump());
    });

    // GET /api/v1/players - 获取玩家列表
    mHttpServer->get(prefix + "/players", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        getSelf().getLogger().trace("[API] /players endpoint called");
        if (!validateToken(req, res)) return;
        
        nlohmann::json json;
        json["players"] = nlohmann::json::array();
        
        auto players = getPlayerCache();
        getSelf().getLogger().debug("[API] /players fetching {} players from cache", players.size());
        for (const auto& player : players) {
            nlohmann::json playerJson;
            playerJson["name"] = player.name;
            playerJson["xuid"] = player.xuid;
            playerJson["uuid"] = player.uuid;
            json["players"].push_back(playerJson);
            getSelf().getLogger().trace("[API] /players including: {}", player.name);
        }
        
        json["count"] = players.size();
        getSelf().getLogger().debug("[API] /players response: count={}", players.size());
        res.setJson(json.dump());
    });

    // GET /api/v1/players/count - 获取玩家数量
    mHttpServer->get(prefix + "/players/count", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        getSelf().getLogger().trace("[API] /players/count endpoint called");
        if (!validateToken(req, res)) return;
        
        nlohmann::json json;
        int count = getPlayerCount();
        json["count"] = count;
        
        getSelf().getLogger().debug("[API] /players/count response: {}", count);
        res.setJson(json.dump());
    });

    // GET /api/v1/players/names - 获取玩家名列表
    mHttpServer->get(prefix + "/players/names", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        getSelf().getLogger().trace("[API] /players/names endpoint called");
        if (!validateToken(req, res)) return;
        
        nlohmann::json json;
        json["names"] = nlohmann::json::array();
        
        auto players = getPlayerCache();
        for (const auto& player : players) {
            json["names"].push_back(player.name);
        }
        
        json["count"] = players.size();
        getSelf().getLogger().debug("[API] /players/names response: {} names", players.size());
        res.setJson(json.dump());
    });

    // GET /api/v1/player/{name} - 获取指定玩家信息
    // 由于简单的路由系统不支持参数，我们使用 query string: /api/v1/player?name=xxx&token=xxx
    mHttpServer->get(prefix + "/player", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        if (!validateToken(req, res)) return;
        
        auto playerName = getQueryParam(req.query, "name");
        
        if (playerName.empty()) {
            getSelf().getLogger().debug("[API] /player request missing 'name' parameter");
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"Missing 'name' parameter\"}");
            return;
        }
        
        getSelf().getLogger().debug("[API] /player querying player: {}", playerName);
        auto playerOpt = getPlayerByName(playerName);
        if (!playerOpt) {
            getSelf().getLogger().debug("[API] /player player not found: {}", playerName);
            res.setStatus(404, "Not Found");
            res.setJson("{\"error\": \"Player not found\"}");
            return;
        }
        getSelf().getLogger().debug("[API] /player found player: {}", playerName);
        
        const auto& player = *playerOpt;
        nlohmann::json json;
        json["name"] = player.name;
        json["xuid"] = player.xuid;
        json["uuid"] = player.uuid;
        json["ipAndPort"] = player.ipAndPort;
        json["locale"] = player.locale;
        json["isOperator"] = player.isOperator;
        json["position"]["x"] = player.posX;
        json["position"]["y"] = player.posY;
        json["position"]["z"] = player.posZ;
        
        res.setJson(json.dump());
    });

    // GET /api/v1/server - 服务器信息
    mHttpServer->get(prefix + "/server", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        getSelf().getLogger().trace("[API] /server endpoint called");
        if (!validateToken(req, res)) return;
        
        nlohmann::json json;
        auto properties = ll::service::getPropertiesSettings();
        json["levelName"] = properties ? static_cast<std::string>(properties->mLevelName) : "Unknown";
        json["bdsVersion"] = ll::getGameVersion().to_string();
        json["protocolVersion"] = ll::getNetworkProtocolVersion();
        json["levilaminaVersion"] = ll::getLoaderVersion().to_string();
        json["pluginVersion"] = PluginVersion;
        json["onlinePlayers"] = getPlayerCount();
        json["maxPlayers"] = getMaxPlayers();
        json["status"] = "running";
        
        getSelf().getLogger().debug("[API] /server response: onlinePlayers={}", json["onlinePlayers"].get<int>());
        res.setJson(json.dump());
    });

    // GET /api/v1/overview - 一次返回查在线所需的同一时刻快照
    mHttpServer->get(prefix + "/overview", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        if (!validateToken(req, res)) return;

        auto players = getPlayerCache();
        std::sort(players.begin(), players.end(), [](const auto& left, const auto& right) {
            return left.name < right.name;
        });
        auto tps = getTpsSnapshot();

        nlohmann::json names = nlohmann::json::array();
        for (const auto& player : players) names.push_back(player.name);

        nlohmann::json json;
        json["status"] = "online";
        json["timestamp"] = unixTimeMs();
        json["uptimeMs"] = getUptimeMs();
        json["tps"] = {
            {"realtime", tps.realtime},
            {"avg10s", tps.avg10s},
            {"avg60s", tps.avg60s},
            {"avg300s", tps.avg300s},
            {"sampledSeconds", tps.sampledSeconds}
        };
        json["players"] = {
            {"online", players.size()},
            {"max", getMaxPlayers()},
            {"names", std::move(names)}
        };
        json["versions"] = {
            {"bds", ll::getGameVersion().to_string()},
            {"protocol", ll::getNetworkProtocolVersion()},
            {"levilamina", ll::getLoaderVersion().to_string()},
            {"plugin", PluginVersion}
        };
        res.setJson(json.dump());
    });

    // GET /api/v1/players/history - 历史玩家分页列表
    mHttpServer->get(prefix + "/players/history", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        if (!validateToken(req, res)) return;
        if (!mPlayerDataStore) {
            res.setStatus(503, "Service Unavailable");
            res.setJson("{\"error\": \"Player data store is unavailable\"}");
            return;
        }

        const auto page = parseBoundedInt(getQueryParam(req.query, "page"), 1, 1, 1000000);
        const auto pageSize = parseBoundedInt(getQueryParam(req.query, "pageSize"), 30, 1, 100);
        auto history = mPlayerDataStore->history(page, pageSize, unixTimeMs());
        nlohmann::json players = nlohmann::json::array();
        for (const auto& player : history.players) players.push_back(playerRecordJson(player));

        const auto pageCount = std::max<std::size_t>(
            1,
            (history.total + static_cast<std::size_t>(history.pageSize) - 1) / static_cast<std::size_t>(history.pageSize)
        );
        nlohmann::json json;
        json["total"] = history.total;
        json["page"] = history.page;
        json["pageSize"] = history.pageSize;
        json["pageCount"] = pageCount;
        json["players"] = std::move(players);
        res.setJson(json.dump());
    });

    // GET /api/v1/players/stats?name=<name-or-xuid> - 历史玩家统计
    mHttpServer->get(prefix + "/players/stats", [this, validateToken](const HttpRequest& req, HttpResponse& res) {
        if (!validateToken(req, res)) return;
        if (!mPlayerDataStore) {
            res.setStatus(503, "Service Unavailable");
            res.setJson("{\"error\": \"Player data store is unavailable\"}");
            return;
        }
        auto name = getQueryParam(req.query, "name");
        if (name.empty()) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"Missing 'name' parameter\"}");
            return;
        }
        auto player = mPlayerDataStore->findPlayer(name, unixTimeMs());
        if (!player) {
            res.setStatus(404, "Not Found");
            res.setJson("{\"error\": \"Historical player not found\"}");
            return;
        }
        res.setJson(playerRecordJson(*player).dump());
    });

    // POST /api/v1/whitelist/bind - 一对一绑定聊天账号与玩家白名单
    mHttpServer->post(prefix + "/whitelist/bind", [this, validateAdminToken, parseJsonBody](
        const HttpRequest& req,
        HttpResponse& res
    ) {
        if (!validateAdminToken(req, res)) return;
        if (!mConfig.enableWhitelistBinding) {
            res.setStatus(403, "Forbidden");
            res.setJson("{\"error\": \"Whitelist binding is disabled\"}");
            return;
        }
        if (!mPlayerDataStore) {
            res.setStatus(503, "Service Unavailable");
            res.setJson("{\"error\": \"Player data store is unavailable\"}");
            return;
        }
        auto body = parseJsonBody(req, res);
        if (!body) return;

        auto stringField = [&](const char* key) {
            auto it = body->find(key);
            return it != body->end() && it->is_string() ? it->get<std::string>() : std::string{};
        };
        auto platform = stringField("platform");
        auto selfId = stringField("selfId");
        auto userId = stringField("userId");
        auto channelId = stringField("channelId");
        auto playerName = stringField("playerName");
        if (platform.empty() || selfId.empty() || userId.empty() || !validPlayerName(playerName)) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"platform, selfId, userId and a valid playerName are required\"}");
            return;
        }

        auto binding = mPlayerDataStore->bindWhitelist(platform, selfId, userId, channelId, playerName, unixTimeMs());
        if (!binding.success) {
            res.setStatus(409, "Conflict");
            res.setJson(nlohmann::json{{"error", binding.error}}.dump());
            return;
        }
        savePlayerData(true);

        auto command = executeCommandOnServerThread("allowlist add \"" + binding.binding.playerName + "\"", mConfig.commandTimeoutMs);
        nlohmann::json json;
        json["success"] = true;
        json["created"] = binding.created;
        json["binding"] = bindingJson(binding.binding);
        json["allowlistUpdated"] = command.success;
        json["commandOutput"] = truncateUtf8(
            command.output,
            static_cast<std::size_t>(std::clamp(mConfig.commandOutputLimit, 256, 32000))
        );
        if (command.timedOut) {
            json["warning"] = "Binding was stored, but the allowlist command timed out";
            res.setStatus(504, "Gateway Timeout");
        } else if (!command.success) {
            json["warning"] = "Binding was stored, but the allowlist command reported a failure";
        }
        getSelf().getLogger().info(
            "[Whitelist] {}:{} bound to {}",
            binding.binding.platform,
            binding.binding.userId,
            binding.binding.playerName
        );
        res.setJson(json.dump());
    });

    // POST /api/v1/whitelist/unbind - 解除当前聊天账号绑定
    mHttpServer->post(prefix + "/whitelist/unbind", [this, validateAdminToken, parseJsonBody](
        const HttpRequest& req,
        HttpResponse& res
    ) {
        if (!validateAdminToken(req, res)) return;
        if (!mConfig.enableWhitelistBinding || !mPlayerDataStore) {
            res.setStatus(403, "Forbidden");
            res.setJson("{\"error\": \"Whitelist binding is unavailable\"}");
            return;
        }
        auto body = parseJsonBody(req, res);
        if (!body) return;
        auto platformIt = body->find("platform");
        auto selfIdIt = body->find("selfId");
        auto userIdIt = body->find("userId");
        if (platformIt == body->end() || !platformIt->is_string()
            || selfIdIt == body->end() || !selfIdIt->is_string()
            || userIdIt == body->end() || !userIdIt->is_string()) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"platform, selfId and userId are required\"}");
            return;
        }
        auto binding = mPlayerDataStore->unbindWhitelist(
            platformIt->get<std::string>(),
            selfIdIt->get<std::string>(),
            userIdIt->get<std::string>()
        );
        if (!binding) {
            res.setStatus(404, "Not Found");
            res.setJson("{\"error\": \"Whitelist binding not found\"}");
            return;
        }
        const auto allowlistRetained = mPlayerDataStore->hasWhitelistAuthorization(binding->playerName);
        savePlayerData(true);
        auto command = allowlistRetained
            ? CommandExecutionResult{true, false, "allowlist retained because another authorization remains"}
            : executeCommandOnServerThread("allowlist remove \"" + binding->playerName + "\"", mConfig.commandTimeoutMs);
        nlohmann::json json;
        json["success"] = true;
        json["binding"] = bindingJson(*binding);
        json["allowlistRetained"] = allowlistRetained;
        json["allowlistUpdated"] = command.success;
        json["commandOutput"] = truncateUtf8(
            command.output,
            static_cast<std::size_t>(std::clamp(mConfig.commandOutputLimit, 256, 32000))
        );
        if (command.timedOut) json["warning"] = "Binding was removed, but the allowlist command timed out";
        res.setJson(json.dump());
    });

    // POST /api/v1/whitelist/add - 管理员直接授权玩家白名单
    mHttpServer->post(prefix + "/whitelist/add", [this, validateAdminToken, parseJsonBody](
        const HttpRequest& req,
        HttpResponse& res
    ) {
        if (!validateAdminToken(req, res)) return;
        if (!mPlayerDataStore) {
            res.setStatus(503, "Service Unavailable");
            res.setJson("{\"error\": \"Player data store is unavailable\"}");
            return;
        }
        auto body = parseJsonBody(req, res);
        if (!body) return;
        auto playerIt = body->find("playerName");
        if (playerIt == body->end() || !playerIt->is_string() || !validPlayerName(playerIt->get<std::string>())) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"a valid playerName is required\"}");
            return;
        }
        auto requesterIt = body->find("requester");
        auto requester = requesterIt != body->end() && requesterIt->is_string()
            ? requesterIt->get<std::string>()
            : std::string{"unknown"};
        auto [grant, created] = mPlayerDataStore->addAdminWhitelist(
            playerIt->get<std::string>(),
            requester,
            unixTimeMs()
        );
        savePlayerData(true);
        auto command = executeCommandOnServerThread("allowlist add \"" + grant.playerName + "\"", mConfig.commandTimeoutMs);
        nlohmann::json json;
        json["success"] = true;
        json["created"] = created;
        json["playerName"] = grant.playerName;
        json["allowlistUpdated"] = command.success;
        json["commandOutput"] = truncateUtf8(
            command.output,
            static_cast<std::size_t>(std::clamp(mConfig.commandOutputLimit, 256, 32000))
        );
        if (command.timedOut) json["warning"] = "Admin grant was stored, but the allowlist command timed out";
        else if (!command.success) json["warning"] = "Admin grant was stored, but the allowlist command reported a failure";
        getSelf().getLogger().info("[Whitelist] Admin {} added {}", requester, grant.playerName);
        res.setJson(json.dump());
    });

    // POST /api/v1/whitelist/remove - 管理员移除玩家白名单及现有绑定
    mHttpServer->post(prefix + "/whitelist/remove", [this, validateAdminToken, parseJsonBody](
        const HttpRequest& req,
        HttpResponse& res
    ) {
        if (!validateAdminToken(req, res)) return;
        if (!mPlayerDataStore) {
            res.setStatus(503, "Service Unavailable");
            res.setJson("{\"error\": \"Player data store is unavailable\"}");
            return;
        }
        auto body = parseJsonBody(req, res);
        if (!body) return;
        auto playerIt = body->find("playerName");
        if (playerIt == body->end() || !playerIt->is_string() || !validPlayerName(playerIt->get<std::string>())) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"a valid playerName is required\"}");
            return;
        }
        auto playerName = playerIt->get<std::string>();
        auto requesterIt = body->find("requester");
        auto requester = requesterIt != body->end() && requesterIt->is_string()
            ? requesterIt->get<std::string>()
            : std::string{"unknown"};
        auto recordRemoved = mPlayerDataStore->revokePlayerWhitelist(playerName);
        savePlayerData(true);
        auto command = executeCommandOnServerThread("allowlist remove \"" + playerName + "\"", mConfig.commandTimeoutMs);
        nlohmann::json json;
        json["success"] = true;
        json["playerName"] = playerName;
        json["recordRemoved"] = recordRemoved;
        json["allowlistUpdated"] = command.success;
        json["commandOutput"] = truncateUtf8(
            command.output,
            static_cast<std::size_t>(std::clamp(mConfig.commandOutputLimit, 256, 32000))
        );
        if (command.timedOut) json["warning"] = "Local authorization was removed, but the allowlist command timed out";
        getSelf().getLogger().info("[Whitelist] Admin {} removed {}", requester, playerName);
        res.setJson(json.dump());
    });

    // POST /api/v1/admin/command - 受控执行 BDS 管理命令
    mHttpServer->post(prefix + "/admin/command", [this, validateAdminToken, parseJsonBody](
        const HttpRequest& req,
        HttpResponse& res
    ) {
        if (!validateAdminToken(req, res)) return;
        if (!mConfig.enableCommandExecution) {
            res.setStatus(403, "Forbidden");
            res.setJson("{\"error\": \"Remote command execution is disabled\"}");
            return;
        }
        auto body = parseJsonBody(req, res);
        if (!body) return;
        auto commandIt = body->find("command");
        if (commandIt == body->end() || !commandIt->is_string()) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"command is required\"}");
            return;
        }
        auto rawCommand = commandIt->get<std::string>();
        if (rawCommand.find_first_of("\r\n") != std::string::npos || rawCommand.size() > 512) {
            res.setStatus(400, "Bad Request");
            res.setJson("{\"error\": \"command must be one line and at most 512 bytes\"}");
            return;
        }
        auto command = trimCommand(std::move(rawCommand));
        if (command.empty() || !commandAllowed(command, mConfig.commandAllowPrefixes)) {
            res.setStatus(403, "Forbidden");
            res.setJson("{\"error\": \"command is not allowed\"}");
            return;
        }

        auto requester = body->find("requester");
        auto requesterText = requester != body->end() && requester->is_string()
            ? requester->get<std::string>()
            : std::string{"unknown"};
        getSelf().getLogger().info("[Admin] Remote command from {}: {}", requesterText, command);
        auto result = executeCommandOnServerThread(command, mConfig.commandTimeoutMs);
        if (result.timedOut) res.setStatus(504, "Gateway Timeout");
        nlohmann::json json;
        json["success"] = result.success;
        json["timedOut"] = result.timedOut;
        json["command"] = command;
        json["output"] = truncateUtf8(
            std::move(result.output),
            static_cast<std::size_t>(std::clamp(mConfig.commandOutputLimit, 256, 32000))
        );
        res.setJson(json.dump());
    });

    // GET /api/v1/health - 健康检查端点 (不需要 token，用于监控)
    mHttpServer->get(prefix + "/health", [this](const HttpRequest&, HttpResponse& res) {
        getSelf().getLogger().trace("[API] /health endpoint called");
        nlohmann::json json;
        json["status"] = "healthy";
        json["timestamp"] = unixTimeMs();
        json["uptime"] = getUptimeMs();
        res.setJson(json.dump());
    });

    // GET / - 根路径，返回 API 信息
    mHttpServer->get("/", [this, prefix](const HttpRequest&, HttpResponse& res) {
        getSelf().getLogger().trace("[API] / (root) endpoint called");
        nlohmann::json json;
        json["name"] = "serverinfo-rest";
        json["version"] = PluginVersion;
        json["description"] = "REST API for Minecraft Bedrock Server information";
        json["endpoints"] = {
            {"GET " + prefix + "/status", "Server status overview"},
            {"GET " + prefix + "/overview", "Combined online status snapshot"},
            {"GET " + prefix + "/health", "Health check"},
            {"GET " + prefix + "/server", "Server information"},
            {"GET " + prefix + "/players", "List all online players"},
            {"GET " + prefix + "/players/count", "Get online player count"},
            {"GET " + prefix + "/players/names", "Get list of player names"},
            {"GET " + prefix + "/player?name=<name>", "Get specific online player information"},
            {"GET " + prefix + "/players/history?page=<page>", "Get historical players"},
            {"GET " + prefix + "/players/stats?name=<name>", "Get historical player statistics"},
            {"POST " + prefix + "/whitelist/bind", "Bind a chat account to the BDS allowlist"},
            {"POST " + prefix + "/whitelist/unbind", "Remove a chat account allowlist binding"},
            {"POST " + prefix + "/whitelist/add", "Add an administrator-managed allowlist grant"},
            {"POST " + prefix + "/whitelist/remove", "Remove a player allowlist grant and binding"},
            {"POST " + prefix + "/admin/command", "Execute an authorized BDS command"}
        };
        res.setJson(json.dump(2));
    });

    logger.info("serverinfo-rest enabled successfully!");
    logger.info("REST API available at http://{}:{}{}", mConfig.host, mConfig.port, prefix);
    return true;
}

bool ServerInfoRestMod::disable() {
    auto& logger = getSelf().getLogger();
    logger.info("Disabling serverinfo-rest...");
    
    // 移除事件监听器
    logger.debug("Removing event listeners...");
    auto& eventBus = ll::event::EventBus::getInstance();
    if (mPlayerJoinListener) {
        eventBus.removeListener(mPlayerJoinListener);
        mPlayerJoinListener = nullptr;
        logger.debug("PlayerJoinEvent listener removed");
    }
    if (mPlayerLeaveListener) {
        eventBus.removeListener(mPlayerLeaveListener);
        mPlayerLeaveListener = nullptr;
        logger.debug("PlayerDisconnectEvent listener removed");
    }
    if (mServerTickListener) {
        eventBus.removeListener(mServerTickListener);
        mServerTickListener = nullptr;
        logger.debug("ServerLevelTickEvent listener removed");
    }
    if (mPlayerConnectListener) {
        eventBus.removeListener(mPlayerConnectListener);
        mPlayerConnectListener = nullptr;
        logger.debug("PlayerConnectEvent listener removed");
    }
    if (mPlayerDestroyBlockListener) {
        eventBus.removeListener(mPlayerDestroyBlockListener);
        mPlayerDestroyBlockListener = nullptr;
        logger.debug("PlayerDestroyBlockEvent listener removed");
    }
    if (mMobDieListener) {
        eventBus.removeListener(mMobDieListener);
        mMobDieListener = nullptr;
        logger.debug("MobDieEvent listener removed");
    }

    const auto nowMs = unixTimeMs();
    for (const auto& player : getPlayerCache()) {
        if (mPlayerDataStore) mPlayerDataStore->playerLeft(player.xuid, nowMs);
    }
    savePlayerData(true);
    
    // 清空玩家缓存
    logger.debug("Clearing player cache...");
    {
        std::lock_guard<std::mutex> lock(mPlayerCacheMutex);
        int cacheSize = static_cast<int>(mPlayerCache.size());
        mPlayerCache.clear();
        logger.debug("Player cache cleared ({} entries removed)", cacheSize);
    }
    
    if (mHttpServer) {
        logger.debug("Stopping HTTP server...");
        mHttpServer->stop();
        mHttpServer.reset();
        logger.debug("HTTP server stopped and released");
    }
    
    logger.info("serverinfo-rest disabled!");
    return true;
}

bool ServerInfoRestMod::unload() {
    auto& logger = getSelf().getLogger();
    logger.info("Unloading serverinfo-rest...");
    logger.debug("Plugin resources released");
    logger.info("serverinfo-rest unloaded successfully!");
    return true;
}

LL_REGISTER_MOD(ServerInfoRestMod, ServerInfoRestMod::getInstance());

} // namespace serverinfo_rest
