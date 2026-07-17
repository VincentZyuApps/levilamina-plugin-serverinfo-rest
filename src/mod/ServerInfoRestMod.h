#pragma once

#include "mod/Config.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/event/ListenerBase.h"
#include <memory>
#include <mutex>
#include <deque>
#include <chrono>
#include <unordered_map>
#include <string>
#include <vector>
#include <optional>

namespace serverinfo_rest {

class HttpServer;

// 缓存的玩家信息结构
struct CachedPlayerInfo {
    std::string name;
    std::string xuid;
    std::string uuid;
    std::string ipAndPort;
    std::string locale;
    bool isOperator = false;
    float posX = 0, posY = 0, posZ = 0;
};

struct TpsSnapshot {
    double realtime = 0.0;
    double avg10s = 0.0;
    double avg60s = 0.0;
    double avg300s = 0.0;
    int sampledSeconds = 0;
};

class ServerInfoRestMod {
public:
    static ServerInfoRestMod& getInstance();

    ServerInfoRestMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

    [[nodiscard]] const Config& getConfig() const { return mConfig; }
    [[nodiscard]] HttpServer* getHttpServer() const { return mHttpServer.get(); }

    // 线程安全的玩家缓存访问
    std::vector<CachedPlayerInfo> getPlayerCache() const;
    std::optional<CachedPlayerInfo> getPlayerByName(const std::string& name) const;
    int getPlayerCount() const;
    TpsSnapshot getTpsSnapshot() const;
    int getMaxPlayers() const;
    long long getUptimeMs() const;

private:
    ll::mod::NativeMod& mSelf;
    Config mConfig;
    std::unique_ptr<HttpServer> mHttpServer;

    // 玩家缓存 (线程安全)
    mutable std::mutex mPlayerCacheMutex;
    std::unordered_map<std::string, CachedPlayerInfo> mPlayerCache; // key = xuid

    // 事件监听器
    ll::event::ListenerPtr mPlayerJoinListener;
    ll::event::ListenerPtr mPlayerLeaveListener;
    ll::event::ListenerPtr mServerTickListener;

    mutable std::mutex mTpsMutex;
    std::deque<double> mTpsSamples;
    std::chrono::steady_clock::time_point mTpsBucketStartedAt = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point mStartedAt = std::chrono::steady_clock::now();
    unsigned int mTicksInCurrentBucket = 0;

    // 缓存更新方法
    void onPlayerJoin(const std::string& xuid, const CachedPlayerInfo& info);
    void onPlayerLeave(const std::string& xuid);
    void onServerTick();
};

} // namespace serverinfo_rest
