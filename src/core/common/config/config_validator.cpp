#include "core/common/config/config_manager.hpp"

namespace iotgw {
namespace core {
namespace common {
namespace config {

bool ValidateBasicConfig(const ConfigManager& cfg, std::string& error) {
  if (!cfg.Has("network.http_api.port")) {
    error = "missing network.http_api.port";
    return false;
  }
  if (!cfg.Has("paths.www_root")) {
    error = "missing paths.www_root";
    return false;
  }
  error.clear();
  return true;
}

}  // namespace config
}  // namespace common
}  // namespace core
}  // namespace iotgw
