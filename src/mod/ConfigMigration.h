#pragma once

#include <nlohmann/json_fwd.hpp>

namespace serverinfo_rest {

bool migrateConfigV4ToV5(nlohmann::ordered_json& data);

} // namespace serverinfo_rest
