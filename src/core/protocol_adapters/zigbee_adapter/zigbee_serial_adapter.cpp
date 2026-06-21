#include "core/protocol_adapters/zigbee_adapter/zigbee_serial_adapter.hpp"

#include "core/common/utils/time_utils.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace iotgw {
namespace core {
namespace protocol_adapters {
namespace zigbee_adapter {

namespace {

speed_t BaudrateToSpeed(int baudrate) {
  switch (baudrate) {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    default:
      return B9600;
  }
}

}  // namespace

constexpr std::size_t kMaxBytesPerPoll = 2048;

ZigbeeSerialAdapter::ZigbeeSerialAdapter(
    std::shared_ptr<core::common::log::Logger> logger)
    : logger_(std::move(logger)) {}

ZigbeeSerialAdapter::~ZigbeeSerialAdapter() {
  Close();
}

bool ZigbeeSerialAdapter::Open(const Options& options) {
  Close();
  options_ = options;
  fd_ = open(options_.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    last_error_ = "open_failed: " + std::string(std::strerror(errno));
    if (logger_) {
      logger_->Warn("ZigBee serial open failed: " + options_.device + " " +
                    std::strerror(errno));
    }
    return false;
  }
  if (!ConfigurePort()) {
    Close();
    return false;
  }
  line_buffer_.clear();
  if (logger_) {
    logger_->Info("ZigBee serial opened device=" + options_.device +
                  " baudrate=" + std::to_string(options_.baudrate));
  }
  return true;
}

void ZigbeeSerialAdapter::Close() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  line_buffer_.clear();
}

bool ZigbeeSerialAdapter::IsOpen() const {
  return fd_ >= 0;
}

const ZigbeeSerialAdapter::Options& ZigbeeSerialAdapter::GetOptions() const {
  return options_;
}

std::uint64_t ZigbeeSerialAdapter::RxBytes() const {
  return rx_bytes_;
}

std::uint64_t ZigbeeSerialAdapter::RxLines() const {
  return rx_lines_;
}

std::uint64_t ZigbeeSerialAdapter::TxBytes() const {
  return tx_bytes_;
}

std::uint64_t ZigbeeSerialAdapter::TxLines() const {
  return tx_lines_;
}

int64_t ZigbeeSerialAdapter::LastRxMs() const {
  return last_rx_ms_;
}

int64_t ZigbeeSerialAdapter::LastTxMs() const {
  return last_tx_ms_;
}

std::string ZigbeeSerialAdapter::LastError() const {
  return last_error_;
}

bool ZigbeeSerialAdapter::SendLine(const std::string& line) {
  if (fd_ < 0) {
    last_error_ = "write_failed: not_open";
    return false;
  }
  std::string out = line;
  if (out.empty() || out.back() != '\n') {
    out.push_back('\n');
  }
  const char* data = out.data();
  std::size_t remaining = out.size();
  int retry = 0;
  while (remaining > 0) {
    const ssize_t written = write(fd_, data, remaining);
    if (written > 0) {
      data += written;
      remaining -= static_cast<std::size_t>(written);
      tx_bytes_ += static_cast<std::uint64_t>(written);
      retry = 0;
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
        retry++ < 50) {
      usleep(1000);
      continue;
    }
    last_error_ = "write_failed: " + std::string(std::strerror(errno));
    if (logger_) {
      logger_->Warn("ZigBee serial write failed: " + std::string(std::strerror(errno)));
    }
    return false;
  }
  ++tx_lines_;
  last_tx_ms_ = core::common::time::NowUnixMs();
  last_error_.clear();
  return true;
}

void ZigbeeSerialAdapter::Poll() {
  if (fd_ < 0) {
    return;
  }
  char buf[256];
  std::size_t bytes_this_poll = 0;
  while (true) {
    const ssize_t n = read(fd_, buf, sizeof(buf));
    if (n > 0) {
      bytes_this_poll += static_cast<std::size_t>(n);
      rx_bytes_ += static_cast<std::uint64_t>(n);
      last_rx_ms_ = core::common::time::NowUnixMs();
      for (ssize_t i = 0; i < n; ++i) {
        HandleIncomingByte(buf[i]);
      }
      if (bytes_this_poll >= kMaxBytesPerPoll) {
        return;
      }
      continue;
    }
    if (n == 0) {
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return;
    }
    last_error_ = "read_failed: " + std::string(std::strerror(errno));
    if (logger_) {
      logger_->Warn("ZigBee serial read failed: " + std::string(std::strerror(errno)));
    }
    Close();
    return;
  }
}

void ZigbeeSerialAdapter::SetLineHandler(LineHandler handler) {
  line_handler_ = std::move(handler);
}

bool ZigbeeSerialAdapter::ConfigurePort() {
  termios tio {};
  if (tcgetattr(fd_, &tio) != 0) {
    if (logger_) {
      logger_->Warn("ZigBee tcgetattr failed: " + std::string(std::strerror(errno)));
    }
    return false;
  }

  const speed_t speed = BaudrateToSpeed(options_.baudrate);
  cfsetispeed(&tio, speed);
  cfsetospeed(&tio, speed);

  tio.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON | IXOFF | IXANY);
  tio.c_oflag &= ~OPOST;
  tio.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  tio.c_cflag |= CLOCAL | CREAD;
  tio.c_cflag &= ~PARENB;
  tio.c_cflag &= ~CSTOPB;
  tio.c_cflag &= ~CSIZE;
  tio.c_cflag |= CS8;
#if defined(CRTSCTS)
  tio.c_cflag &= ~CRTSCTS;
#endif
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (tcsetattr(fd_, TCSANOW, &tio) != 0) {
    if (logger_) {
      logger_->Warn("ZigBee tcsetattr failed: " + std::string(std::strerror(errno)));
    }
    return false;
  }
  tcflush(fd_, TCIOFLUSH);
  return true;
}

void ZigbeeSerialAdapter::HandleIncomingByte(char ch) {
  if (ch == '\r') {
    return;
  }
  if (ch == '\n') {
    if (!line_buffer_.empty() && line_handler_) {
      ++rx_lines_;
      line_handler_(line_buffer_);
    }
    line_buffer_.clear();
    return;
  }
  if (line_buffer_.size() >= options_.max_line_size) {
    const int64_t now = core::common::time::NowUnixMs();
    if (logger_ && now - last_line_too_long_warn_ms_ >= 3000) {
      logger_->Warn("ZigBee serial line too long, dropped");
      last_line_too_long_warn_ms_ = now;
    }
    line_buffer_.clear();
    return;
  }
  line_buffer_.push_back(ch);
}

}  // namespace zigbee_adapter
}  // namespace protocol_adapters
}  // namespace core
}  // namespace iotgw
