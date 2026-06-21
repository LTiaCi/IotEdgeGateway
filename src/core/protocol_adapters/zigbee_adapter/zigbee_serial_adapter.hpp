#pragma once

#include "core/common/logger/logger.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace iotgw {
namespace core {
namespace protocol_adapters {
namespace zigbee_adapter {

class ZigbeeSerialAdapter {
 public:
  struct Options {
    std::string device = "/dev/ttyS4";
    int baudrate = 9600;
    std::size_t max_line_size = 1024;
  };

  using LineHandler = std::function<void(const std::string& line)>;

  explicit ZigbeeSerialAdapter(
      std::shared_ptr<core::common::log::Logger> logger);
  ~ZigbeeSerialAdapter();

  bool Open(const Options& options);
  void Close();
  bool IsOpen() const;
  const Options& GetOptions() const;
  std::uint64_t RxBytes() const;
  std::uint64_t RxLines() const;
  std::uint64_t TxBytes() const;
  std::uint64_t TxLines() const;
  int64_t LastRxMs() const;
  int64_t LastTxMs() const;
  std::string LastError() const;

  bool SendLine(const std::string& line);
  void Poll();
  void SetLineHandler(LineHandler handler);

 private:
  bool ConfigurePort();
  void HandleIncomingByte(char ch);

  int fd_ = -1;
  Options options_;
  std::string line_buffer_;
  LineHandler line_handler_;
  std::uint64_t rx_bytes_ = 0;
  std::uint64_t rx_lines_ = 0;
  std::uint64_t tx_bytes_ = 0;
  std::uint64_t tx_lines_ = 0;
  int64_t last_rx_ms_ = 0;
  int64_t last_tx_ms_ = 0;
  int64_t last_line_too_long_warn_ms_ = 0;
  std::string last_error_;
  std::shared_ptr<core::common::log::Logger> logger_;
};

}  // namespace zigbee_adapter
}  // namespace protocol_adapters
}  // namespace core
}  // namespace iotgw
