#include "services/web_services/api/rest_api.hpp"

#include "core/common/utils/json_utils.hpp"

namespace iotgw {
namespace services {
namespace web_services {
namespace api {

namespace {

ApiResponse ResultToResponse(
    const services::system_services::camera::CameraResult& result,
    int ok_status = 200) {
  namespace json = iotgw::core::common::json;
  return {result.ok ? ok_status : 400, "application/json",
          json::Object({{"ok", json::Bool(result.ok)},
                        {"message", json::Quote(result.message)},
                        {"url", json::Quote(result.url)}})};
}

}  // namespace

ApiResponse HandleCameraApi(const std::string& method,
                            const std::string& path,
                            ApiContext& ctx) {
  if (!ctx.camera) {
    return {};
  }
  if (method == "GET" && path == "/api/camera/status") {
    return ResultToResponse(ctx.camera->Status());
  }
  if (method == "POST" && path == "/api/camera/start_stream") {
    return ResultToResponse(ctx.camera->StartStream());
  }
  if (method == "POST" && path == "/api/camera/stop_stream") {
    return ResultToResponse(ctx.camera->StopStream());
  }
  if (method == "POST" && path == "/api/camera/snapshot") {
    return ResultToResponse(ctx.camera->Snapshot());
  }
  if (method == "POST" && path == "/api/camera/start_record") {
    return ResultToResponse(ctx.camera->StartRecord());
  }
  if (method == "POST" && path == "/api/camera/stop_record") {
    return ResultToResponse(ctx.camera->StopRecord());
  }
  return {};
}

}  // namespace api
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
