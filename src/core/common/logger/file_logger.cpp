#include "core/common/logger/logger.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>

#if defined(_WIN32)
#include <direct.h>
#endif

namespace iotgw {
namespace core {
namespace common {
namespace log {

namespace {

bool MakeDir(const std::string& path) {
  if (path.empty()) {
    return true;
  }
#if defined(_WIN32)
  if (_mkdir(path.c_str()) == 0 || errno == EEXIST) {
    return true;
  }
#else
  if (mkdir(path.c_str(), 0755) == 0 || errno == EEXIST) {
    return true;
  }
#endif
  return false;
}

void EnsureParentDirs(const std::string& file_path) {
  std::string normalized = file_path;
  for (char& ch : normalized) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  std::size_t pos = 0;
  while (true) {
    pos = normalized.find('/', pos + 1);
    if (pos == std::string::npos) {
      break;
    }
    MakeDir(normalized.substr(0, pos));
  }
}

}  // namespace

FileSink::FileSink(const std::string& path) {
  EnsureParentDirs(path);
  out_.open(path.c_str(), std::ios::app);
}

bool FileSink::IsOpen() const {
  std::lock_guard<std::mutex> lock(mu_);
  return out_.is_open();
}

void FileSink::Write(Level level, const std::string& message) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!out_.is_open()) {
    return;
  }
  out_ << common::time::NowLocalText() << " [" << LevelName(level) << "] "
       << message << std::endl;
}

void FileSink::Flush() {
  std::lock_guard<std::mutex> lock(mu_);
  if (out_.is_open()) {
    out_.flush();
  }
}

}  // namespace log
}  // namespace common
}  // namespace core
}  // namespace iotgw
