#pragma once

#include <cstdint>
#include <string>

namespace iotgw {
namespace core {
namespace device {
namespace model {

struct DeviceStatus {
  bool online = false;
  int64_t last_seen_ms = 0;
  std::string last_payload;
  std::string last_topic;
};

}  // namespace model
}  // namespace device
}  // namespace core
}  // namespace iotgw
