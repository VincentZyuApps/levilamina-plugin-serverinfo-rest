#pragma once

#include <string>

namespace serverinfo_rest {

struct CommandExecutionResult {
    bool success = false;
    bool timedOut = false;
    std::string output;
};

CommandExecutionResult executeCommandOnServerThread(const std::string& command, int timeoutMs);

} // namespace serverinfo_rest
