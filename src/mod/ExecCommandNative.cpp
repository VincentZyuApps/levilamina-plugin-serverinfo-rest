#include "mod/ExecCommandNative.h"

#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/commands/Command.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputType.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CurrentCmdVersion.h"
#include "mc/server/commands/MinecraftCommands.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/Minecraft.h"
#include "mc/world/level/dimension/VanillaDimensions.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <string_view>

namespace serverinfo_rest {
namespace {

CommandExecutionResult executeCommandNative(const std::string& commandText) {
    auto minecraft = ll::service::getMinecraft();
    auto level = ll::service::getLevel();
    if (!minecraft || !minecraft->mCommands || !level) {
        return {false, false, "server not ready"};
    }

    auto& serverLevel = static_cast<::ServerLevel&>(*level);
    auto origin = ::ServerCommandOrigin(
        "serverinfo-rest",
        serverLevel,
        ::CommandPermissionLevel::Owner,
        ::VanillaDimensions::Overworld()
    );

    std::string compileError;
    auto command = minecraft->mCommands->compileCommand(
        ::HashedString(std::string_view(commandText)),
        origin,
        ::CurrentCmdVersion::Latest,
        [&](const std::string& error) { compileError.append(error).append("\n"); }
    );
    if (!command) {
        if (!compileError.empty() && compileError.back() == '\n') compileError.pop_back();
        return {false, false, compileError.empty() ? "command compile failed" : compileError};
    }

    ::CommandOutput output(::CommandOutputType::AllOutput);
    command->run(origin, output);

    static std::shared_ptr<const ::Localization> locale =
        ::getI18n().getLocaleFor(::getI18n().getCurrentLanguage()->mCode);
    std::string outputText;
    for (const auto& message : output.mMessages) {
        if (!outputText.empty()) outputText += '\n';
        outputText += ::getI18n().get(message.mMessageId, message.mParams, locale);
    }
    if (outputText.empty()) outputText = output.mSuccessCount > 0 ? "command completed" : "command failed";
    return {output.mSuccessCount > 0, false, std::move(outputText)};
}

} // namespace

CommandExecutionResult executeCommandOnCurrentServerThread(const std::string& command) {
    try {
        return executeCommandNative(command);
    } catch (const std::exception& error) {
        return {false, false, error.what()};
    } catch (...) {
        return {false, false, "unknown command execution error"};
    }
}

CommandExecutionResult executeCommandOnServerThread(const std::string& command, int timeoutMs) {
    auto promise = std::make_shared<std::promise<CommandExecutionResult>>();
    auto future = promise->get_future();
    ll::thread::ServerThreadExecutor::getDefault().execute([promise, command] {
        try {
            promise->set_value(executeCommandOnCurrentServerThread(command));
        } catch (const std::exception& error) {
            promise->set_value({false, false, error.what()});
        } catch (...) {
            promise->set_value({false, false, "unknown command execution error"});
        }
    });

    const auto timeout = std::chrono::milliseconds(std::clamp(timeoutMs, 100, 30000));
    if (future.wait_for(timeout) != std::future_status::ready) {
        return {false, true, "command execution timed out"};
    }
    return future.get();
}

} // namespace serverinfo_rest
