#pragma once

#include <string>
#include <vector>

namespace serverinfo_rest {

struct Config {
    int version = 3;

    std::string _comment_logLevel = "日志级别：silent | fatal | error | warn | info | debug | trace";
    std::string logLevel = "info";

    std::string _comment_host = "HTTP 监听地址：0.0.0.0 表示允许其他设备访问，127.0.0.1 表示仅本机访问";
    std::string host = "0.0.0.0";

    std::string _comment_port = "HTTP 监听端口，修改后需要同步更新客户端连接地址";
    int port = 60202;

    std::string _comment_enableCors = "是否返回 CORS 响应头；浏览器跨域访问时需要启用";
    bool enableCors = true;

    std::string _comment_apiPrefix = "API 路径前缀，客户端必须配置为相同值";
    std::string apiPrefix = "/api/v1";

    std::string _comment_enableToken = "是否要求只读查询接口验证 token；健康检查接口始终无需 token";
    bool enableToken = false;

    std::string _comment_token = "只读查询令牌；enableToken=false 时可以留空，建议通过 Authorization Bearer 传递";
    std::string token = "";

    std::string _comment_adminToken = "管理令牌；绑定、解绑、添加、移除白名单时必须填写，禁止与只读 token 相同";
    std::string adminToken = "";

    std::string _comment_enableCommandExecution = "是否开放远程 BDS 命令接口；白名单接口不受此开关影响";
    bool enableCommandExecution = false;

    std::string _comment_commandAllowPrefixes = "远程命令允许前缀；空数组表示不额外限制，仅在命令接口启用时生效";
    std::vector<std::string> commandAllowPrefixes{};

    std::string _comment_commandTimeoutMs = "BDS 命令执行等待上限，单位毫秒";
    int commandTimeoutMs = 5000;

    std::string _comment_commandOutputLimit = "BDS 命令返回文本最大长度";
    int commandOutputLimit = 4000;

    std::string _comment_enableWhitelistBinding = "是否开放聊天账号绑定与解绑白名单接口";
    bool enableWhitelistBinding = true;

    std::string _comment_enforceWhitelistBinding = "是否拒绝没有普通绑定或管理员直接授权的玩家进服";
    bool enforceWhitelistBinding = true;

    std::string _comment_operatorBypassBinding = "OP 是否跳过绑定检查；false 表示 OP 也必须获得授权";
    bool operatorBypassBinding = false;

    std::string _comment_whitelistDataFailurePolicy =
        "玩家数据与备份都损坏时的策略：fail-open 暂停拦截 | fail-closed 拒绝无法验证的玩家";
    std::string whitelistDataFailurePolicy = "fail-open";

    std::string _comment_repairMissingAllowlistEntriesOnStartup =
        "启动时补齐插件记录中缺失的 BDS 白名单，不会删除 BDS 中的额外白名单";
    bool repairMissingAllowlistEntriesOnStartup = true;

    std::string _comment_dataSaveIntervalSeconds = "玩家历史与统计数据自动保存周期，单位秒";
    int dataSaveIntervalSeconds = 60;
};

} // namespace serverinfo_rest
