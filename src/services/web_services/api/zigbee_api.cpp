#include "services/web_services/api/rest_api.hpp"

#include "core/common/utils/json_utils.hpp"
#include "core/common/utils/time_utils.hpp"

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

std::string NormalizeZigbeePayload(const std::string& id,
                                   const std::string& body) {
  namespace json = iotgw::core::common::json;
  if (id == "led") {
    return json::Object({
        {"device_id", json::Quote("led")},
        {"type", json::Quote("cmd")},
        {"data", json::Object({
                     {"on", ExtractControlPayloadValue(body, "on", "0")},
                     {"br", ExtractControlPayloadValue(body, "br", "50")},
                 })},
    });
  }
  if (id == "motor") {
    return json::Object({
        {"device_id", json::Quote("motor")},
        {"type", json::Quote("cmd")},
        {"data", json::Object({
                     {"on", ExtractControlPayloadValue(body, "on", "0")},
                     {"sp", ExtractControlPayloadValue(body, "sp", "30")},
                     {"dir", ExtractControlPayloadValue(body, "dir", "0")},
                 })},
    });
  }
  return json::Object({
      {"device_id", json::Quote("buzzer")},
      {"type", json::Quote("cmd")},
      {"data", json::Object({{"on", ExtractControlPayloadValue(body, "on", "0")}})},
  });
}

void UpdateControlState(const std::string& id,
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

bool IsSupportedActuator(const std::string& id) {
  return id == "led" || id == "motor" || id == "buzzer";
}

}  // namespace

ApiResponse HandleZigbeeApi(const std::string& method,
                            const std::string& path,
                            const std::string& body,
                            ApiContext& ctx) {
  namespace json = iotgw::core::common::json;
  if (method == "GET" && path == "/api/zigbee/status") {
    const bool open = ctx.zigbee && ctx.zigbee->IsOpen();
    const std::string device =
        ctx.zigbee ? ctx.zigbee->GetOptions().device : "/dev/ttyS4";
    const int baudrate =
        ctx.zigbee ? ctx.zigbee->GetOptions().baudrate : 9600;
    const std::uint64_t rx_bytes = ctx.zigbee ? ctx.zigbee->RxBytes() : 0;
    const std::uint64_t rx_lines = ctx.zigbee ? ctx.zigbee->RxLines() : 0;
    const std::uint64_t tx_bytes = ctx.zigbee ? ctx.zigbee->TxBytes() : 0;
    const std::uint64_t tx_lines = ctx.zigbee ? ctx.zigbee->TxLines() : 0;
    const int64_t last_rx_ms = ctx.zigbee ? ctx.zigbee->LastRxMs() : 0;
    const int64_t last_tx_ms = ctx.zigbee ? ctx.zigbee->LastTxMs() : 0;
    const std::string last_error =
        ctx.zigbee ? ctx.zigbee->LastError() : "zigbee_not_initialized";
    return {200, "application/json",
            json::Object({{"ok", json::Bool(open)},
                          {"open", json::Bool(open)},
                          {"device", json::Quote(device)},
                          {"baudrate", json::Number(baudrate)},
                          {"rx_bytes", json::Number(rx_bytes)},
                          {"rx_lines", json::Number(rx_lines)},
                          {"tx_bytes", json::Number(tx_bytes)},
                          {"tx_lines", json::Number(tx_lines)},
                          {"last_rx_ms", json::Number(last_rx_ms)},
                          {"last_tx_ms", json::Number(last_tx_ms)},
                          {"last_error", json::Quote(last_error)}})};
  }

  const std::string prefix = "/api/zigbee/actuators/";
  if (method != "POST" || !StartsWith(path, prefix) ||
      path.find("/set") == std::string::npos) {
    return {};
  }

  const std::size_t begin = prefix.size();
  const std::size_t end = path.find("/set", begin);
  const std::string id = path.substr(begin, end - begin);
  if (!IsSupportedActuator(id)) {
    return {404, "application/json",
            json::Object({{"ok", "false"},
                          {"error", json::Quote("actuator_not_found")}})};
  }

  const std::string payload = NormalizeZigbeePayload(id, body);
  std::string topic = "zigbee:/dev/ttyS4/" + id;
  if (ctx.zigbee) {
    topic = "zigbee:" + ctx.zigbee->GetOptions().device + "/" + id;
  }

  const bool sent = ctx.zigbee && ctx.zigbee->IsOpen() &&
                    ctx.zigbee->SendLine(payload);
  if (ctx.database) {
    ctx.database->RecordCommand(id, topic, payload, sent,
                                core::common::time::NowUnixMs());
  }
  if (!sent) {
    return {503, "application/json",
            json::Object({{"ok", "false"},
                          {"error", json::Quote("zigbee_not_connected")}})};
  }

  UpdateControlState(id, body, ctx.control_state);
  return {200, "application/json", json::Object({{"ok", "true"}})};
}

}  // namespace api
}  // namespace web_services
}  // namespace services
}  // namespace iotgw
