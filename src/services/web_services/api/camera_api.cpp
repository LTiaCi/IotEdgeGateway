#include "services/web_services/api/rest_api.hpp"

#include "core/common/utils/json_utils.hpp"
#include "core/common/utils/time_utils.hpp"

#include <fstream>
#include <sstream>

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

bool ReadFileBytes(const std::string& file_path, std::string& data) {
  std::ifstream in(file_path.c_str(), std::ios::binary);
  if (!in) {
    return false;
  }
  std::ostringstream out;
  out << in.rdbuf();
  data = out.str();
  return true;
}

}  // namespace

ApiResponse HandleCameraApi(const std::string& method,
                            const std::string& path,
                            ApiContext& ctx) {
  if (!ctx.camera) {
    return {};
  }
  const std::string route = path.substr(0, path.find('?'));
  if (method == "GET" && route == "/api/camera/status") {
    return ResultToResponse(ctx.camera->Status());
  }
  if (method == "GET" && route == "/api/camera/preview.jpg") {
    const auto result = ctx.camera->PreviewFrame();
    if (!result.ok) {
      return ResultToResponse(result, 200);
    }
    std::string data;
    if (!ReadFileBytes(result.file_path, data)) {
      return {500, "application/json",
              "{\"ok\":false,\"message\":\"preview_read_failed\"}"};
    }
    return {200, "image/jpeg", data};
  }
  if (method == "POST" && route == "/api/camera/start_stream") {
    return ResultToResponse(ctx.camera->StartStream());
  }
  if (method == "POST" && route == "/api/camera/stop_stream") {
    return ResultToResponse(ctx.camera->StopStream());
  }
  if (method == "POST" && route == "/api/camera/snapshot") {
    const auto result = ctx.camera->Snapshot();
    if (ctx.database && result.ok) {
      ctx.database->RecordMediaFile("snapshot", result.url, result.file_path,
                                    result.message,
                                    core::common::time::NowUnixMs());
    }
    return ResultToResponse(result);
  }
  if (method == "POST" && route == "/api/camera/start_record") {
    const auto result = ctx.camera->StartRecord();
    if (ctx.database && result.ok) {
      ctx.database->RecordMedia("record_start", result.url, result.file_path,
                                result.message,
                                core::common::time::NowUnixMs());
    }
    return ResultToResponse(result);
  }
  if (method == "POST" && route == "/api/camera/stop_record") {
    const auto result = ctx.camera->StopRecord();
    if (ctx.database && result.ok) {
      ctx.database->RecordMediaFile("record", result.url, result.file_path,
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
