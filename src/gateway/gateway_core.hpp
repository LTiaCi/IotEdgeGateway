#pragma once

#include <string>
#include <vector>

namespace iotgw {
namespace gateway {

struct GatewayArgs {
  std::string yaml_config = "config/environments/development.yaml";
  std::string log_file;
  std::string log_level;
  bool print_version = false;
  std::string set_version;
};

class GatewayCore {
 public:
  int Run(const GatewayArgs& args);
};

GatewayArgs ParseArgs(int argc, char** argv);
void PrintUsage();

}  // namespace gateway
}  // namespace iotgw
