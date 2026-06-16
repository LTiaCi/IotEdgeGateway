#pragma once

#include "core/device/model/device_entity.hpp"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace iotgw {
namespace core {
namespace device {
namespace manager {

class DeviceRegistry {
 public:
  bool Register(model::DeviceEntity device);
  bool Has(const std::string& id) const;
  bool Get(const std::string& id, model::DeviceEntity& out) const;
  std::vector<model::DeviceEntity> List() const;

  bool UpdateFromTelemetryTopic(const std::string& topic,
                                const std::string& payload,
                                int64_t now_ms,
                                std::string& out_device_id);

  bool UpsertMqttDeviceFromTopic(const std::string& topic,
                                 const std::string& payload,
                                 int64_t now_ms,
                                 std::string& out_device_id);

  bool GetCommandTopic(const std::string& device_id, std::string& out_topic) const;
  bool GetTelemetryTopic(const std::string& device_id, std::string& out_topic) const;

  std::string ToJsonList() const;
  bool ToJsonOne(const std::string& id, std::string& out_json) const;
  std::string StatusJson() const;
  std::string DashboardStatusJson() const;

 private:
  static std::string ExtractIdFromTopic(const std::string& topic);
  static std::string DeviceToJson(const model::DeviceEntity& device);
  void ReindexLocked(const model::DeviceEntity& device);

  mutable std::mutex mu_;
  std::map<std::string, model::DeviceEntity> by_id_;
  std::map<std::string, std::string> telemetry_topic_to_id_;
  std::map<std::string, std::string> command_topic_to_id_;
};

}  // namespace manager
}  // namespace device
}  // namespace core
}  // namespace iotgw
