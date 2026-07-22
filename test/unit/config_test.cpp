#include "mod/Config.h"

#include <gtest/gtest.h>

namespace serverinfo_rest {
namespace {

TEST(ConfigTest, DefaultsToApiVersionTwoContract) {
    const Config config;
    EXPECT_EQ(config.version, 6);
    EXPECT_EQ(config.apiPrefix, "/api/v2");
}

} // namespace
} // namespace serverinfo_rest
