#include "services/web_services/api/rest_api.hpp"

#include "core/common/utils/json_utils.hpp"

#include <cstdlib>

namespace iotgw {
namespace services {
namespace web_services {
namespace api {

namespace {

int LimitFromPath(const std::string& path) {
  const std::string key = "limit=";
  const std::size_t pos = path.find(key);
  if (pos == std::string::npos) {
    return 50;
  }
  return std::atoi(path.substr(pos + key.size()).c_str());
}

bool PathBaseEquals(const std::string& path, const std::string& base) {
  return path == base || path.find(base + "?") == 0;
}

ApiResponse RowsResponse(const std::string& rows_json) {
  namespace json = iotgw::core::common::json;
  return {200, "application/json", json::Object({{"ok", "true"}, {"rows", rows_json}})};
}

}  // namespace

ApiResponse HandleDatabaseApi(const std::string& method,
                              const std::string& path,
                              ApiContext& ctx) {
  namespace json = iotgw::core::common::json;
  if (!ctx.database) {
    return {};
  }
  if (method != "GET") {
    return {};
  }
  if (path == "/api/db/summary") {
    return {200, "application/json", ctx.database->SummaryJson()};
  }
  if (PathBaseEquals(path, "/api/db/sensors")) {
    return RowsResponse(ctx.database->RecentSensorJson(LimitFromPath(path)));
  }
  if (PathBaseEquals(path, "/api/db/media")) {
    return RowsResponse(ctx.database->RecentMediaJson(LimitFromPath(path)));
  }
  if (PathBaseEquals(path, "/api/db/commands")) {
    return RowsResponse(ctx.database->RecentCommandsJson(LimitFromPath(path)));
  }
  if (PathBaseEquals(path, "/api/db/logs")) {
    return RowsResponse(ctx.database->RecentLogsJson(LimitFromPath(path)));
  }
  return {};
}

}  // namespace api
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
