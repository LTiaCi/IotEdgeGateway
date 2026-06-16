#include "services/web_services/api/rest_api.hpp"

#include "core/common/utils/json_utils.hpp"

namespace iotgw {
namespace services {
namespace web_services {
namespace api {

ApiResponse HandleSystemApi(const std::string& method,
                            const std::string& path,
                            const std::string& version) {
  namespace json = iotgw::core::common::json;
  if (method == "GET" && path == "/api/health") {
    return {200, "application/json", json::Object({{"status", json::Quote("ok")}})};
  }
  if (method == "GET" && path == "/api/version") {
    return {200, "application/json",
            json::Object({{"version", json::Quote(version)}})};
  }
  return {};
}

}  // namespace api
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
