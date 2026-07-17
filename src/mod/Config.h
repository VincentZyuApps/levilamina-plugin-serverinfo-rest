#pragma once

#include <string>
#include <vector>

namespace serverinfo_rest {

struct Config {
    int version = 2;
    
    // 日志级别: "silent", "fatal", "error", "warn", "info", "debug", "trace"
    std::string logLevel = "info";
    
    // HTTP 服务器配置
    std::string host = "0.0.0.0";
    int port = 60202;
    
    // 是否启用 CORS (跨域资源共享)
    bool enableCors = true;
    
    // API 路径前缀
    std::string apiPrefix = "/api/v1";
    
    // Token 认证配置
    bool enableToken = false;  // 是否启用 token 验证
    std::string token = "";    // 推荐使用 Authorization: Bearer <token>，query token 仅用于兼容

    // 管理 API 使用独立令牌，禁止复用只读 token
    std::string adminToken = "";

    // 远程命令默认关闭；allowPrefixes 为空时允许全部命令
    bool enableCommandExecution = false;
    std::vector<std::string> commandAllowPrefixes{};
    int commandTimeoutMs = 5000;
    int commandOutputLimit = 4000;

    // 群聊账号与 BDS 白名单绑定
    bool enableWhitelistBinding = true;
    bool enforceWhitelistBinding = true;
    bool operatorBypassBinding = true;

    // 玩家历史与统计数据保存周期
    int dataSaveIntervalSeconds = 60;
};

} // namespace serverinfo_rest
