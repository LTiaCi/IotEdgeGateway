#include "services/web_services/api/rest_api.hpp"

#include "core/common/utils/json_utils.hpp"
#include "core/common/utils/time_utils.hpp"

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
                        {"url", json::Quote(result.url)},
                        {"file_path", json::Quote(result.file_path)}})};
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
    const auto result = ctx.camera->Snapshot();
    if (ctx.database && result.ok) {
      ctx.database->RecordMedia("snapshot", result.url, result.file_path,
                                result.message,
                                core::common::time::NowUnixMs());
    }
    return ResultToResponse(result);
  }
  if (method == "POST" && path == "/api/camera/start_record") {
    const auto result = ctx.camera->StartRecord();
    if (ctx.database && result.ok) {
      ctx.database->RecordMedia("record_start", result.url, result.file_path,
                                result.message,
                                core::common::time::NowUnixMs());
    }
    return ResultToResponse(result);
  }
  if (method == "POST" && path == "/api/camera/stop_record") {
    const auto result = ctx.camera->StopRecord();
    if (ctx.database && result.ok) {
      ctx.database->RecordMedia("record", result.url, result.file_path,
                                result.message,
                                core::common::time::NowUnixMs());
    }
    return ResultToResponse(result);
  }
  return {};
}

}  // namespace api
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
