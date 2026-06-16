#include "core/device/manager/device_manager.hpp"

#include "core/common/utils/json_utils.hpp"

#include <sstream>

namespace iotgw {
namespace core {
namespace device {
namespace manager {

namespace json = iotgw::core::common::json;

bool DeviceRegistry::Register(model::DeviceEntity device) {
  if (device.id.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mu_);
  by_id_[device.id] = device;
  ReindexLocked(by_id_[device.id]);
  return true;
}

bool DeviceRegistry::Has(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mu_);
  return by_id_.count(id) != 0;
}

bool DeviceRegistry::Get(const std::string& id, model::DeviceEntity& out) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = by_id_.find(id);
  if (it == by_id_.end()) {
    return false;
  }
  out = it->second;
  return true;
}

std::vector<model::DeviceEntity> DeviceRegistry::List() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<model::DeviceEntity> out;
  for (const auto& kv : by_id_) {
    out.push_back(kv.second);
  }
  return out;
}

bool DeviceRegistry::UpdateFromTelemetryTopic(const std::string& topic,
                                              const std::string& payload,
                                              int64_t now_ms,
                                              std::string& out_device_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto index_it = telemetry_topic_to_id_.find(topic);
  if (index_it == telemetry_topic_to_id_.end()) {
    out_device_id.clear();
    return false;
  }
  auto device_it = by_id_.find(index_it->second);
  if (device_it == by_id_.end()) {
    out_device_id.clear();
    return false;
  }
  auto& status = device_it->second.status;
  status.online = true;
  status.last_seen_ms = now_ms;
  status.last_payload = payload;
  status.last_topic = topic;
  out_device_id = device_it->second.id;
  return true;
}

bool DeviceRegistry::UpsertMqttDeviceFromTopic(const std::string& topic,
                                               const std::string& payload,
                                               int64_t now_ms,
                                               std::string& out_device_id) {
  if (UpdateFromTelemetryTopic(topic, payload, now_ms, out_device_id)) {
    return true;
  }

  const std::string id = ExtractIdFromTopic(topic);
  if (id.empty()) {
    out_device_id.clear();
    return false;
  }

  model::DeviceEntity device;
  device.id = id;
  device.kind = topic.find("telemetry") != std::string::npos ? "sensor" : "unknown";
  device.transport = "mqtt";
  device.telemetry_topic = topic;
  device.command_topic = "";
  device.status.online = true;
  device.status.last_seen_ms = now_ms;
  device.status.last_payload = payload;
  device.status.last_topic = topic;
  Register(device);
  out_device_id = id;
  return true;
}

bool DeviceRegistry::GetCommandTopic(const std::string& device_id,
                                     std::string& out_topic) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = by_id_.find(device_id);
  if (it == by_id_.end() || it->second.command_topic.empty()) {
    return false;
  }
  out_topic = it->second.command_topic;
  return true;
}

bool DeviceRegistry::GetTelemetryTopic(const std::string& device_id,
                                       std::string& out_topic) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = by_id_.find(device_id);
  if (it == by_id_.end() || it->second.telemetry_topic.empty()) {
    return false;
  }
  out_topic = it->second.telemetry_topic;
  return true;
}

std::string DeviceRegistry::ToJsonList() const {
  const auto devices = List();
  std::vector<std::string> values;
  for (const auto& device : devices) {
    values.push_back(DeviceToJson(device));
  }
  return json::Array(values);
}

bool DeviceRegistry::ToJsonOne(const std::string& id, std::string& out_json) const {
  model::DeviceEntity device;
  if (!Get(id, device)) {
    return false;
  }
  out_json = DeviceToJson(device);
  return true;
}

std::string DeviceRegistry::StatusJson() const {
  const auto devices = List();
  std::ostringstream oss;
  oss << "{";
  for (std::size_t i = 0; i < devices.size(); ++i) {
    if (i != 0) {
      oss << ",";
    }
    oss << json::Quote(devices[i].id) << ":"
        << json::Quote(devices[i].status.last_payload);
  }
  oss << "}";
  return oss.str();
}

std::string DeviceRegistry::DashboardStatusJson() const {
  namespace json = iotgw::core::common::json;
  const auto devices = List();
  std::map<std::string, std::string> values;
  for (const auto& device : devices) {
    double value = 0.0;
    if (json::GetJsonNumber(device.status.last_payload, "value", value)) {
      values[device.id] = json::Number(value);
    }
  }

  auto get = [&](const std::string& id, const std::string& fallback) {
    auto it = values.find(id);
    return it == values.end() ? fallback : it->second;
  };

  return json::Object({
      {"temp", get("temp", json::Quote("--"))},
      {"humi", get("humi", json::Quote("--"))},
      {"light", get("light", json::Quote("--"))},
      {"ir", get("ir", "0")},
  });
}

std::string DeviceRegistry::ExtractIdFromTopic(const std::string& topic) {
  const std::size_t pos = topic.find_last_of('/');
  if (pos == std::string::npos) {
    return topic;
  }
  if (pos + 1 >= topic.size()) {
    return "";
  }
  return topic.substr(pos + 1);
}

std::string DeviceRegistry::DeviceToJson(const model::DeviceEntity& device) {
  const std::string status = json::Object({
      {"online", json::Bool(device.status.online)},
      {"last_seen_ms", json::Number(device.status.last_seen_ms)},
      {"last_topic", json::Quote(device.status.last_topic)},
      {"last_payload", json::Quote(device.status.last_payload)},
  });

  return json::Object({
      {"id", json::Quote(device.id)},
      {"kind", json::Quote(device.kind)},
      {"transport", json::Quote(device.transport)},
      {"telemetry_topic", json::Quote(device.telemetry_topic)},
      {"command_topic", json::Quote(device.command_topic)},
      {"status", status},
  });
}

void DeviceRegistry::ReindexLocked(const model::DeviceEntity& device) {
  if (!device.telemetry_topic.empty()) {
    telemetry_topic_to_id_[device.telemetry_topic] = device.id;
  }
  if (!device.command_topic.empty()) {
    command_topic_to_id_[device.command_topic] = device.id;
  }
}

}  // namespace manager
}  // namespace device
}  // namespace core
}  // namespace iotgw
