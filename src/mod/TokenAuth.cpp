#include "mod/TokenAuth.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace serverinfo_rest {

namespace {

std::string normalizeMode(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    std::string normalized(value.substr(first, last - first + 1));
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized;
}

} // namespace

std::optional<TokenReceiveMode> parseTokenReceiveMode(std::string_view value) {
    const auto normalized = normalizeMode(value);
    if (normalized == "param") return TokenReceiveMode::Param;
    if (normalized == "header") return TokenReceiveMode::Header;
    if (normalized == "both") return TokenReceiveMode::Both;
    return std::nullopt;
}

std::string_view tokenReceiveModeName(TokenReceiveMode mode) {
    switch (mode) {
    case TokenReceiveMode::Param:
        return "param";
    case TokenReceiveMode::Header:
        return "header";
    case TokenReceiveMode::Both:
        return "both";
    }
    return "header";
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

TokenSelection selectToken(std::string headerToken, std::string paramToken, TokenReceiveMode mode) {
    if (mode == TokenReceiveMode::Header) return {std::move(headerToken), false};
    if (mode == TokenReceiveMode::Param) return {std::move(paramToken), false};

    if (!headerToken.empty() && !paramToken.empty() && !secureEquals(headerToken, paramToken)) {
        return {{}, true};
    }
    return {!headerToken.empty() ? std::move(headerToken) : std::move(paramToken), false};
}

} // namespace serverinfo_rest
