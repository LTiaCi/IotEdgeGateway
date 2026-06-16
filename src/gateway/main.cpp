#include "gateway/gateway_core.hpp"

#include "version.hpp"

#include <cstdlib>
#include <iostream>

namespace iotgw {
namespace gateway {

GatewayArgs ParseArgs(int argc, char** argv) {
  GatewayArgs args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](std::string* target) {
      if (i + 1 < argc) {
        *target = argv[++i];
      }
    };
    if (arg == "--yaml-config") {
      require_value(&args.yaml_config);
    } else if (arg == "--log-file") {
      require_value(&args.log_file);
    } else if (arg == "--log-level") {
      require_value(&args.log_level);
    } else if (arg == "--print-version") {
      args.print_version = true;
    } else if (arg == "--set-version") {
      require_value(&args.set_version);
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      std::exit(0);
    }
  }
  return args;
}

void PrintUsage() {
  std::cout << "iotgw_gateway [options]\n"
            << "  --yaml-config <path>  YAML config path\n"
            << "  --log-file <path>     Log file path\n"
            << "  --log-level <level>   trace/debug/info/warn/error/fatal\n"
            << "  --print-version       Print version and exit\n"
            << "  --set-version <ver>   Override runtime version string\n";
}

}  // namespace gateway
}  // namespace iotgw

int main(int argc, char** argv) {
  auto args = iotgw::gateway::ParseArgs(argc, argv);
  if (args.print_version) {
    std::cout << (args.set_version.empty() ? IOTGW_VERSION : args.set_version)
              << std::endl;
    return 0;
  }
  iotgw::gateway::GatewayCore core;
  return core.Run(args);
}
