#pragma once

#include "core/common/logger/logger.hpp"

#include <memory>
#include <string>

namespace iotgw {
namespace services {
namespace system_services {
namespace camera {

struct CameraOptions {
  std::string device = "/dev/video9";
  std::string stream_www_root = "/mnt/www";
  std::string stream_root = "/mnt/www/stream";
  std::string media_root = "/userdata/www";
  int width = 1280;
  int height = 720;
  int fps = 10;
};

struct CameraResult {
  bool ok = false;
  std::string message;
  std::string url;
};

class CameraManager {
 public:
  explicit CameraManager(std::shared_ptr<core::common::log::Logger> logger);
  ~CameraManager();

  bool Init(const CameraOptions& options);
  CameraResult StartStream();
  CameraResult StopStream();
  CameraResult Snapshot();
  CameraResult StartRecord();
  CameraResult StopRecord();
  CameraResult Status() const;

 private:
  bool EnsureDirs() const;
  bool FileExists(const std::string& path) const;
  std::string MakeMediaName(const std::string& prefix,
                            const std::string& ext);
  std::string MediaPath(const std::string& filename) const;
  std::string MediaUrl(const std::string& filename) const;
  std::string StreamPlaylistPath() const;
  std::string StreamPidPath() const;
  std::string RecordPidPath() const;
  int RunCommand(const std::string& command) const;

  CameraOptions options_;
  bool initialized_ = false;
  bool streaming_ = false;
  bool recording_ = false;
  int media_sequence_ = 0;
  std::string current_record_file_;
  std::shared_ptr<core::common::log::Logger> logger_;
};

}  // namespace camera
}  // namespace system_services
}  // namespace services
}  // namespace iotgw
