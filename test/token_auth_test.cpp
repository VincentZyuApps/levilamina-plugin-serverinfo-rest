#include "mod/TokenAuth.h"

#include <gtest/gtest.h>

namespace serverinfo_rest {
namespace {

TEST(TokenAuthTest, ParsesSupportedModes) {
    EXPECT_EQ(parseTokenReceiveMode("param"), TokenReceiveMode::Param);
    EXPECT_EQ(parseTokenReceiveMode(" HEADER "), TokenReceiveMode::Header);
    EXPECT_EQ(parseTokenReceiveMode("Both"), TokenReceiveMode::Both);
    EXPECT_FALSE(parseTokenReceiveMode("unknown").has_value());
}

TEST(TokenAuthTest, SelectsOnlyTheConfiguredSource) {
    auto header = selectToken("header-token", "param-token", TokenReceiveMode::Header);
    EXPECT_EQ(header.token, "header-token");
    EXPECT_FALSE(header.conflict);

    auto param = selectToken("header-token", "param-token", TokenReceiveMode::Param);
    EXPECT_EQ(param.token, "param-token");
    EXPECT_FALSE(param.conflict);
}

TEST(TokenAuthTest, AcceptsEitherSourceInBothMode) {
    EXPECT_EQ(selectToken("header-token", {}, TokenReceiveMode::Both).token, "header-token");
    EXPECT_EQ(selectToken({}, "param-token", TokenReceiveMode::Both).token, "param-token");

    auto matching = selectToken("same-token", "same-token", TokenReceiveMode::Both);
    EXPECT_EQ(matching.token, "same-token");
    EXPECT_FALSE(matching.conflict);
}

TEST(TokenAuthTest, RejectsConflictingSourcesInBothMode) {
    auto selection = selectToken("header-token", "param-token", TokenReceiveMode::Both);
    EXPECT_TRUE(selection.token.empty());
    EXPECT_TRUE(selection.conflict);
}

TEST(TokenAuthTest, ComparesTokensWithoutEarlyExit) {
    EXPECT_TRUE(secureEquals("token", "token"));
    EXPECT_FALSE(secureEquals("token", "Token"));
    EXPECT_FALSE(secureEquals("short", "longer"));
}

} // namespace
} // namespace serverinfo_rest
