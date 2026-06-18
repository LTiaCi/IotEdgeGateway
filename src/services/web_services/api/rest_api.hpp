#pragma once

#include "core/control/rule_engine.hpp"
#include "core/device/manager/device_manager.hpp"
#include "core/protocol_adapters/mqtt_adapter/mqtt_adapter.hpp"
#include "services/system_services/camera/camera_manager.hpp"
#include "services/storage/database_service.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace iotgw {
namespace services {
namespace web_services {
namespace api {

struct ControlState {
  int led_on = 0;
  int led_br = 50;
  int motor_on = 0;
  int motor_sp = 30;
  int motor_dir = 0;
  int buzzer = 0;
  std::mutex mu;
};

struct ApiContext {
  core::device::manager::DeviceRegistry* devices = nullptr;
  core::control::RuleEngine* rules = nullptr;
  core::protocol_adapters::mqtt_adapter::MqttClient* mqtt = nullptr;
  system_services::camera::CameraManager* camera = nullptr;
  storage::DatabaseService* database = nullptr;
  ControlState* control_state = nullptr;
  std::function<bool()> reload_rules;
  std::function<void(const std::string& topic, const std::string& payload)>
      ingest_telemetry;
  std::string version;
};

struct ApiResponse {
  int status = 404;
  std::string content_type = "application/json";
  std::string body = "{\"error\":\"not_found\"}";
};

ApiResponse HandleSystemApi(const std::string& method,
                            const std::string& path,
                            const std::string& version);

ApiResponse HandleDeviceApi(const std::string& method,
                            const std::string& path,
                            const std::string& body,
                            ApiContext& ctx);

ApiResponse HandleRuleApi(const std::string& method,
                          const std::string& path,
                          ApiContext& ctx);

ApiResponse HandleCameraApi(const std::string& method,
                            const std::string& path,
                            ApiContext& ctx);

ApiResponse HandleDatabaseApi(const std::string& method,
                              const std::string& path,
                              ApiContext& ctx);

inline ApiResponse DispatchApi(const std::string& method,
                               const std::string& path,
                               const std::string& body,
                               ApiContext& ctx) {
  ApiResponse response = HandleSystemApi(method, path, ctx.version);
  if (response.status != 404) {
    return response;
  }
  response = HandleDeviceApi(method, path, body, ctx);
  if (response.status != 404) {
    return response;
  }
  response = HandleCameraApi(method, path, ctx);
  if (response.status != 404) {
    return response;
  }
  response = HandleDatabaseApi(method, path, ctx);
  if (response.status != 404) {
    return response;
  }
  return HandleRuleApi(method, path, ctx);
}

}  // namespace api
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
