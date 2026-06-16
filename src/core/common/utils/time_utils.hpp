#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

namespace iotgw {
namespace core {
namespace common {
namespace time {

inline int64_t NowUnixMs() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             now.time_since_epoch())
      .count();
}

inline std::string NowIso8601Utc() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t raw = std::chrono::system_clock::to_time_t(now);
  std::tm tm_utc{};
#if defined(_WIN32)
  gmtime_s(&tm_utc, &raw);
#else
  gmtime_r(&raw, &tm_utc);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

inline std::string NowLocalText() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t raw = std::chrono::system_clock::to_time_t(now);
  std::tm tm_local{};
#if defined(_WIN32)
  localtime_s(&tm_local, &raw);
#else
  localtime_r(&raw, &tm_local);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm_local, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

inline void SleepMs(int64_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace time
}  // namespace common
}  // namespace core
}  // namespace iotgw
