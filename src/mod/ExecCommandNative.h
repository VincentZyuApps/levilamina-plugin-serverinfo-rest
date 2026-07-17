#pragma once

#include <string>

namespace serverinfo_rest {

struct CommandExecutionResult {
    bool success = false;
    bool timedOut = false;
    std::string output;
};

CommandExecutionResult executeCommandOnServerThread(const std::string& command, int timeoutMs);

// 仅可由 BDS 主线程调用；用于插件 enable 阶段等已经位于主线程的路径。
CommandExecutionResult executeCommandOnCurrentServerThread(const std::string& command);

} // namespace serverinfo_rest
