#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace serverinfo_rest {

enum class TokenReceiveMode {
    Param,
    Header,
    Both,
};

struct TokenSelection {
    std::string token;
    bool conflict = false;
};

std::optional<TokenReceiveMode> parseTokenReceiveMode(std::string_view value);
std::string_view tokenReceiveModeName(TokenReceiveMode mode);
TokenSelection selectToken(std::string headerToken, std::string paramToken, TokenReceiveMode mode);
bool secureEquals(std::string_view left, std::string_view right);

} // namespace serverinfo_rest
