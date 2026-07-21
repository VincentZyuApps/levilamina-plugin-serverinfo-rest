#include "mod/ConfigMigration.h"

#include <nlohmann/json.hpp>

#include <array>
#include <string_view>

namespace serverinfo_rest {
namespace {

struct ConfigKeyMigration {
    std::string_view oldKey;
    std::string_view newKey;
};

constexpr std::array WhitelistKeyMigrations{
    ConfigKeyMigration{"enableWhitelistBinding", "enableWhitelistBindingApiEndpoints"},
    ConfigKeyMigration{"enforceWhitelistBinding", "requireWhitelistAuthorizationOnJoin"},
    ConfigKeyMigration{"operatorBypassBinding", "operatorBypassesWhitelistAuthorization"},
};

} // namespace

bool migrateConfigV4ToV5(nlohmann::ordered_json& data) {
    const auto version = data.find("version");
    if (version != data.end() && version->is_number_integer() && version->get<int>() > 4) {
        return false;
    }

    bool changed = false;
    for (const auto& migration : WhitelistKeyMigrations) {
        const auto oldKey = std::string(migration.oldKey);
        const auto newKey = std::string(migration.newKey);
        const auto oldValue = data.find(oldKey);
        if (oldValue != data.end()) {
            if (!data.contains(newKey)) data[newKey] = *oldValue;
            data.erase(oldValue);
            changed = true;
        }

        changed = data.erase("_comment_" + oldKey) > 0 || changed;
    }

    if (!data.contains("version") || !data["version"].is_number_integer() || data["version"].get<int>() != 5) {
        data["version"] = 5;
        changed = true;
    }
    return changed;
}

} // namespace serverinfo_rest
