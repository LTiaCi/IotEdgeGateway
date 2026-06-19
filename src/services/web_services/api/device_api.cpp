#include "services/web_services/api/rest_api.hpp"

#include "core/common/utils/json_utils.hpp"
#include "core/common/utils/time_utils.hpp"

#include <mutex>

namespace iotgw {
namespace services {
namespace web_services {
namespace api {

namespace {

bool StartsWith(const std::string& text, const std::string& prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

std::string ExtractControlPayloadValue(const std::string& body,
                                       const std::string& key,
                                       const std::string& fallback) {
  double number = 0.0;
  if (iotgw::core::common::json::GetJsonNumber(body, key, number)) {
    return iotgw::core::common::json::Number(number);
  }
  bool flag = false;
  if (iotgw::core::common::json::GetJsonBool(body, key, flag)) {
    return flag ? "1" : "0";
  }
  return fallback;
}

int ExtractControlInt(const std::string& body,
                      const std::string& key,
                      int fallback) {
  double number = 0.0;
  if (iotgw::core::common::json::GetJsonNumber(body, key, number)) {
    return static_cast<int>(number);
  }
  bool flag = false;
  if (iotgw::core::common::json::GetJsonBool(body, key, flag)) {
    return flag ? 1 : 0;
  }
  return fallback;
}

std::string DashboardStatusWithControl(ApiContext& ctx) {
  namespace json = iotgw::core::common::json;
  if (!ctx.control_state) {
    return ctx.devices->DashboardStatusJson();
  }

  std::string status = ctx.devices->DashboardStatusJson();
  if (!status.empty() && status.back() == '}') {
    status.pop_back();
  }

  std::lock_guard<std::mutex> lock(ctx.control_state->mu);
  if (status.size() > 1) {
    status += ",";
  }
  status += "\"led_on\":" + json::Number(ctx.control_state->led_on);
  status += ",\"led_br\":" + json::Number(ctx.control_state->led_br);
  status += ",\"motor_on\":" + json::Number(ctx.control_state->motor_on);
  status += ",\"motor_sp\":" + json::Number(ctx.control_state->motor_sp);
  status += ",\"motor_dir\":" + json::Number(ctx.control_state->motor_dir);
  status += ",\"buzzer\":" + json::Number(ctx.control_state->buzzer);
  status += "}";
  return status;
}

std::string NormalizeActuatorPayload(const std::string& id,
                                     const std::string& body) {
  namespace json = iotgw::core::common::json;
  if (id == "led") {
    const std::string on = ExtractControlPayloadValue(body, "on", "0");
    const std::string br = ExtractControlPayloadValue(body, "br", "50");
    return json::Object({
        {"device_id", json::Quote("led")},
        {"type", json::Quote("cmd")},
        {"data", json::Object({{"on", on}, {"br", br}})},
    });
  }

  if (id == "motor") {
    const std::string on = ExtractControlPayloadValue(body, "on", "0");
    const std::string sp = ExtractControlPayloadValue(body, "sp", "30");
    const std::string dir = ExtractControlPayloadValue(body, "dir", "0");
    return json::Object({
        {"device_id", json::Quote("motor")},
        {"type", json::Quote("cmd")},
        {"data", json::Object({{"on", on}, {"sp", sp}, {"dir", dir}})},
    });
  }

  bool on = false;
  if (!json::GetJsonBool(body, "on", on)) {
    on = false;
  }

  return json::Object({
      {"device_id", json::Quote("buzzer")},
      {"type", json::Quote("cmd")},
      {"data", json::Object({{"on", on ? "1" : "0"}})},
  });
}

void UpdateActuatorControlState(const std::string& id,
                                const std::string& body,
                                ControlState* state) {
  if (!state) {
    return;
  }
  std::lock_guard<std::mutex> lock(state->mu);
  if (id == "led") {
    state->led_on = ExtractControlInt(body, "on", state->led_on);
    state->led_br = ExtractControlInt(body, "br", state->led_br);
  } else if (id == "motor") {
    state->motor_on = ExtractControlInt(body, "on", state->motor_on);
    state->motor_sp = ExtractControlInt(body, "sp", state->motor_sp);
    state->motor_dir = ExtractControlInt(body, "dir", state->motor_dir);
  } else if (id == "buzzer") {
    state->buzzer = ExtractControlInt(body, "on", state->buzzer);
  }
}

}  // namespace

ApiResponse HandleDeviceApi(const std::string& method,
                            const std::string& path,
                            const std::string& body,
                            ApiContext& ctx) {
  namespace json = iotgw::core::common::json;

  if (method == "GET" && path == "/api/devices" && ctx.devices) {
    return {200, "application/json", ctx.devices->ToJsonList()};
  }

  if (method == "GET" && StartsWith(path, "/api/devices/") && ctx.devices) {
    const std::string id = path.substr(std::string("/api/devices/").size());
    std::string out;
    if (!ctx.devices->ToJsonOne(id, out)) {
      return {404, "application/json",
              json::Object({{"error", json::Quote("device_not_found")}})};
    }
    return {200, "application/json", out};
  }

  if (method == "GET" && path == "/api/status" && ctx.devices) {
    return {200, "application/json", DashboardStatusWithControl(ctx)};
  }

  if (method == "POST" && path == "/api/control") {
    if (body.find("\"type\"") != std::string::npos &&
        body.find("\"control\"") != std::string::npos && ctx.devices) {
      if (ctx.control_state) {
        std::lock_guard<std::mutex> lock(ctx.control_state->mu);
        ctx.control_state->led_on =
            ExtractControlInt(body, "led_on", ctx.control_state->led_on);
        ctx.control_state->led_br =
            ExtractControlInt(body, "led_br", ctx.control_state->led_br);
        ctx.control_state->motor_on =
            ExtractControlInt(body, "motor_on", ctx.control_state->motor_on);
        ctx.control_state->motor_sp =
            ExtractControlInt(body, "motor_sp", ctx.control_state->motor_sp);
        ctx.control_state->motor_dir =
            ExtractControlInt(body, "motor_dir", ctx.control_state->motor_dir);
        ctx.control_state->buzzer =
            ExtractControlInt(body, "buzzer", ctx.control_state->buzzer);
      }

      std::vector<std::pair<std::string, std::string>> commands = {
          {"led", json::Object({{"on", ExtractControlPayloadValue(body, "led_on", "0")},
                                {"br", ExtractControlPayloadValue(body, "led_br", "50")}})},
          {"motor", json::Object({{"on", ExtractControlPayloadValue(body, "motor_on", "0")},
                                  {"sp", ExtractControlPayloadValue(body, "motor_sp", "30")},
                                  {"dir", ExtractControlPayloadValue(body, "motor_dir", "0")}})},
          {"buzzer", json::Object({{"on", ExtractControlPayloadValue(body, "buzzer", "0")}})},
      };

      bool any_published = false;
      for (const auto& command : commands) {
        std::string topic;
        if (!ctx.devices->GetCommandTopic(command.first, topic)) {
          continue;
        }
        const std::string payload = json::Object({
            {"device_id", json::Quote(command.first)},
            {"type", json::Quote("cmd")},
            {"data", command.second},
        });
        const bool published =
            ctx.mqtt && ctx.mqtt->Publish(topic, payload, 0, false);
        if (ctx.database) {
          ctx.database->RecordCommand(command.first, topic, payload, published,
                                      core::common::time::NowUnixMs());
        }
        if (published) {
          any_published = true;
        }
      }

      return {200, "application/json",
              json::Object({{"ok", json::Bool(any_published)},
                            {"mqtt_connected",
                             json::Bool(ctx.mqtt && ctx.mqtt->IsOpen())}})};
    }

    const std::string topic = json::GetJsonString(body, "topic");
    const std::string payload = json::GetJsonString(body, "payload", body);
    if (topic.empty()) {
      return {400, "application/json",
              json::Object({{"status", json::Quote("missing_topic")}})};
    }
    if (ctx.ingest_telemetry) {
      ctx.ingest_telemetry(topic, payload);
    }
    return {200, "application/json", json::Object({{"status", json::Quote("ok")}})};
  }

  if (method == "POST" && StartsWith(path, "/api/actuators/") &&
      path.find("/set") != std::string::npos && ctx.devices) {
    const std::string prefix = "/api/actuators/";
    const std::size_t begin = prefix.size();
    const std::size_t end = path.find("/set", begin);
    const std::string id = path.substr(begin, end - begin);

    std::string topic;
    if (!ctx.devices->GetCommandTopic(id, topic)) {
      return {404, "application/json",
              json::Object({{"ok", "false"},
                            {"error", json::Quote("actuator_not_found")}})};
    }
    const std::string payload = NormalizeActuatorPayload(id, body);
    const bool published = ctx.mqtt && ctx.mqtt->Publish(topic, payload, 0, false);
    if (ctx.database) {
      ctx.database->RecordCommand(id, topic, payload, published,
                                  core::common::time::NowUnixMs());
    }
    if (!published) {
      return {503, "application/json",
              json::Object({{"ok", "false"},
                            {"error", json::Quote("mqtt_not_connected")}})};
    }
    UpdateActuatorControlState(id, body, ctx.control_state);
    return {200, "application/json", json::Object({{"ok", "true"}})};
  }

  return {};
}

}  // namespace api
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
