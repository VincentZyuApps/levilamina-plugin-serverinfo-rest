#include "mod/ServerInfoRestMod.h"
#include "mod/HttpServer.h"

#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/Config.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/Versions.h"
#include "ll/api/io/LogLevel.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/PropertiesSettings.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

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
    std::lock_guard<std::mutex> lock(mTpsMutex);
    ++mTicksInCurrentBucket;
    auto elapsed = std::chrono::duration<double>(now - mTpsBucketStartedAt).count();
    if (elapsed < 1.0) return;

    auto tps = std::clamp(static_cast<double>(mTicksInCurrentBucket) / elapsed, 0.0, 20.0);
    auto elapsedSeconds = std::max(1, static_cast<int>(std::floor(elapsed)));
    for (int i = 0; i < elapsedSeconds; ++i) {
        mTpsSamples.push_back(tps);
    }
    while (mTpsSamples.size() > 300) mTpsSamples.pop_front();
    mTicksInCurrentBucket = 0;
    mTpsBucketStartedAt = now;
}

void ServerInfoRestMod::onPlayerJoin(const std::string& xuid, const CachedPlayerInfo& info) {
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

    // ==================== 创建 HTTP 服务器 ====================
    mHttpServer = std::make_unique<HttpServer>(mConfig.host, mConfig.port, this);
    
    if (!mHttpServer->start()) {
        logger.error("Failed to start HTTP server!");
        return false;
    }

    std::string prefix = mConfig.apiPrefix;

    // Token 验证辅助函数
    auto validateToken = [this](const HttpRequest& req, HttpResponse& res) -> bool {
        if (!mConfig.enableToken) {
            return true; // 未启用 token 验证，直接通过
        }
        
        std::string reqToken;
        auto authorization = getHeaderCaseInsensitive(req, "authorization");
        constexpr std::string_view bearerPrefix = "Bearer ";
        if (authorization.size() >= bearerPrefix.size()
            && std::equal(bearerPrefix.begin(), bearerPrefix.end(), authorization.begin(),
                [](char left, char right) { return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right)); })) {
            reqToken = authorization.substr(bearerPrefix.size());
        }
        if (reqToken.empty()) reqToken = getQueryParam(req.query, "token");
        
        if (reqToken.empty()) {
            res.setStatus(401, "Unauthorized");
            res.setJson("{\"error\": \"Missing token parameter\"}");
            getSelf().getLogger().debug("Request rejected: missing token");
            return false;
        }
        
        if (reqToken != mConfig.token) {
            res.setStatus(403, "Forbidden");
            res.setJson("{\"error\": \"Invalid token\"}");
            getSelf().getLogger().debug("Request rejected: invalid token");
            return false;
        }
        
        return true;
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
            {"GET " + prefix + "/player?name=<name>", "Get specific player information"}
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
