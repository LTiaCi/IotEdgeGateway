#pragma once

#include "core/device/model/device_status.hpp"

#include <string>

namespace iotgw {
namespace core {
namespace device {
namespace model {

struct DeviceEntity {
  std::string id;
  std::string kind = "unknown";
  std::string transport = "mqtt";
  std::string telemetry_topic;
  std::string command_topic;
  DeviceStatus status;
};

}  // namespace model
}  // namespace device
}  // namespace core
}  // namespace iotgw
