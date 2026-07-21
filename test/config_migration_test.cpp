#include "mod/Config.h"
#include "mod/ConfigMigration.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace serverinfo_rest {
namespace {

TEST(ConfigMigrationTest, UsesVersionFiveDefaults) {
    const Config config;

    EXPECT_EQ(config.version, 5);
    EXPECT_TRUE(config.enableWhitelistBindingApiEndpoints);
    EXPECT_TRUE(config.enableWhitelistManagementApiEndpoints);
    EXPECT_TRUE(config.requireWhitelistAuthorizationOnJoin);
    EXPECT_FALSE(config.operatorBypassesWhitelistAuthorization);
}

TEST(ConfigMigrationTest, PreservesLegacyWhitelistValues) {
    nlohmann::ordered_json data{
        {"version", 4},
        {"logLevel", "trace"},
        {"enableWhitelistBinding", false},
        {"enforceWhitelistBinding", false},
        {"operatorBypassBinding", true},
        {"_comment_enableWhitelistBinding", "legacy comment"},
        {"_comment_enforceWhitelistBinding", "legacy comment"},
        {"_comment_operatorBypassBinding", "legacy comment"},
    };

    EXPECT_TRUE(migrateConfigV4ToV5(data));
    EXPECT_EQ(data["version"], 5);
    EXPECT_EQ(data["logLevel"], "trace");
    EXPECT_EQ(data["enableWhitelistBindingApiEndpoints"], false);
    EXPECT_EQ(data["requireWhitelistAuthorizationOnJoin"], false);
    EXPECT_EQ(data["operatorBypassesWhitelistAuthorization"], true);
    EXPECT_FALSE(data.contains("enableWhitelistBinding"));
    EXPECT_FALSE(data.contains("enforceWhitelistBinding"));
    EXPECT_FALSE(data.contains("operatorBypassBinding"));
    EXPECT_FALSE(data.contains("_comment_enableWhitelistBinding"));
    EXPECT_FALSE(data.contains("_comment_enforceWhitelistBinding"));
    EXPECT_FALSE(data.contains("_comment_operatorBypassBinding"));
}

TEST(ConfigMigrationTest, KeepsExplicitNewValuesWhenBothNamesExist) {
    nlohmann::ordered_json data{
        {"version", 4},
        {"enableWhitelistBinding", false},
        {"enableWhitelistBindingApiEndpoints", true},
        {"enforceWhitelistBinding", true},
        {"requireWhitelistAuthorizationOnJoin", false},
        {"operatorBypassBinding", true},
        {"operatorBypassesWhitelistAuthorization", false},
    };

    EXPECT_TRUE(migrateConfigV4ToV5(data));
    EXPECT_EQ(data["enableWhitelistBindingApiEndpoints"], true);
    EXPECT_EQ(data["requireWhitelistAuthorizationOnJoin"], false);
    EXPECT_EQ(data["operatorBypassesWhitelistAuthorization"], false);
}

TEST(ConfigMigrationTest, DoesNotRewriteFutureConfigVersions) {
    nlohmann::ordered_json data{
        {"version", 6},
        {"enableWhitelistBinding", false},
    };

    EXPECT_FALSE(migrateConfigV4ToV5(data));
    EXPECT_EQ(data["version"], 6);
    EXPECT_TRUE(data.contains("enableWhitelistBinding"));
    EXPECT_FALSE(data.contains("enableWhitelistBindingApiEndpoints"));
}

} // namespace
} // namespace serverinfo_rest
