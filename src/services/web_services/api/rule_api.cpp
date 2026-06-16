#include "services/web_services/api/rest_api.hpp"

#include "core/common/utils/json_utils.hpp"

namespace iotgw {
namespace services {
namespace web_services {
namespace api {

namespace {

bool StartsWith(const std::string& text, const std::string& prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

ApiResponse HandleRuleApi(const std::string& method,
                          const std::string& path,
                          ApiContext& ctx) {
  namespace json = iotgw::core::common::json;
  if (!ctx.rules) {
    return {};
  }
  if (method == "GET" && path == "/api/rules") {
    return {200, "application/json", ctx.rules->ToJson()};
  }
  if (method == "POST" && path == "/api/rules/reload") {
    const bool ok = ctx.reload_rules ? ctx.reload_rules() : false;
    return {ok ? 200 : 500, "application/json",
            json::Object({{"ok", json::Bool(ok)}})};
  }
  const std::string prefix = "/api/rules/";
  if (method == "POST" && StartsWith(path, prefix)) {
    const std::size_t action_pos = path.find_last_of('/');
    if (action_pos == std::string::npos || action_pos <= prefix.size()) {
      return {};
    }
    const std::string id = path.substr(prefix.size(), action_pos - prefix.size());
    const std::string action = path.substr(action_pos + 1);
    if (action != "enable" && action != "disable") {
      return {};
    }
    const bool ok = ctx.rules->SetEnabled(id, action == "enable");
    return {ok ? 200 : 404, "application/json",
            json::Object({{"ok", json::Bool(ok)}})};
  }
  return {};
}

}  // namespace api
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
