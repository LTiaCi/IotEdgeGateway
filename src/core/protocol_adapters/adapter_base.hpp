#pragma once

#include <string>

namespace iotgw {
namespace core {
namespace protocol_adapters {

class AdapterBase {
 public:
  virtual ~AdapterBase() = default;
  virtual std::string Name() const = 0;
  virtual bool Start() = 0;
  virtual void Stop() = 0;
};

}  // namespace protocol_adapters
}  // namespace core
}  // namespace iotgw
