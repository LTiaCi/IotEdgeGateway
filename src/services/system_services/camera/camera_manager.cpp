#include "services/system_services/camera/camera_manager.hpp"

#include <ctime>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace iotgw {
namespace services {
namespace system_services {
namespace camera {

CameraManager::CameraManager(std::shared_ptr<core::common::log::Logger> logger)
    : logger_(std::move(logger)) {}

CameraManager::~CameraManager() {
  if (streaming_) {
    StopStream();
  }
}

bool CameraManager::Init(const CameraOptions& options) {
  options_ = options;
#if defined(IOTGW_ENABLE_GSTREAMER)
  initialized_ = EnsureDirs();
#else
  initialized_ = false;
  (void)EnsureDirs();
#endif
  return initialized_;
}

CameraResult CameraManager::StartStream() {
#if !defined(IOTGW_ENABLE_GSTREAMER)
  return {false, "gstreamer_disabled", ""};
#else
  if (!initialized_ && !Init(options_)) {
    return {false, "camera_init_failed", ""};
  }
  if (streaming_) {
    return {true, "already_streaming", "/stream/stream.m3u8"};
  }

  RunCommand("rm -f " + options_.stream_root + "/*.ts " + options_.stream_root +
             "/*.m3u8");

  std::ostringstream pipeline;
  pipeline << "gst-launch-1.0 -e "
           << "v4l2src device=" << options_.device << " "
           << "! video/x-raw,format=YUY2,width=" << options_.width
           << ",height=" << options_.height << ",framerate=" << options_.fps
           << "/1 "
           << "! videoconvert "
           << "! mpph264enc "
           << "! h264parse "
           << "! mpegtsmux "
           << "! hlssink location=" << options_.stream_root
           << "/segment%05d.ts playlist-location=" << StreamPlaylistPath()
           << " target-duration=1 max-files=5";

  const std::string cmd = "sh -c '" + pipeline.str() + " > /tmp/iotgw_gst.log 2>&1 & echo $! > " +
                          StreamPidPath() + "'";
  if (RunCommand(cmd) != 0 || !FileExists(StreamPidPath())) {
    if (logger_) logger_->Error("Camera stream process failed to start");
    return {false, "stream_start_failed", ""};
  }

  streaming_ = true;
  if (logger_) logger_->Info("Camera HLS stream process started");
  return {true, "stream_started", "/stream/stream.m3u8"};
#endif
}

CameraResult CameraManager::StopStream() {
#if !defined(IOTGW_ENABLE_GSTREAMER)
  return {false, "gstreamer_disabled", ""};
#else
  if (!streaming_) {
    return {true, "not_streaming", ""};
  }

  RunCommand("kill -2 $(cat " + StreamPidPath() +
             ") 2>/dev/null; sleep 1; kill $(cat " + StreamPidPath() +
             ") 2>/dev/null; rm -f " + StreamPidPath());
  streaming_ = false;
  RunCommand("rm -f " + options_.stream_root + "/*.ts " + options_.stream_root +
             "/*.m3u8");
  if (logger_) logger_->Info("Camera HLS stream process stopped");
  return {true, "stream_stopped", ""};
#endif
}

CameraResult CameraManager::Snapshot() {
  if (!streaming_) {
    return {false, "stream_not_started", ""};
  }
  const std::string filename = MakeMediaName("snapshot", "jpg");
  const std::string cmd = "ffmpeg -y -i " + StreamPlaylistPath() +
                          " -vframes 1 -q:v 2 " + MediaPath(filename) +
                          " > /dev/null 2>&1";
  if (RunCommand(cmd) != 0) {
    return {false, "snapshot_failed", ""};
  }
  return {true, "snapshot_saved", MediaUrl(filename)};
}

CameraResult CameraManager::StartRecord() {
  if (!streaming_) {
    return {false, "stream_not_started", ""};
  }
  if (recording_) {
    return {true, "already_recording", MediaUrl(current_record_file_)};
  }
  current_record_file_ = MakeMediaName("record", "mp4");
  const std::string cmd = "ffmpeg -y -i " + StreamPlaylistPath() +
                          " -c copy " + MediaPath(current_record_file_) +
                          " > /dev/null 2>&1 & echo $! > " + RecordPidPath();
  if (RunCommand(cmd) != 0) {
    current_record_file_.clear();
    return {false, "record_start_failed", ""};
  }
  recording_ = true;
  return {true, "record_started", MediaUrl(current_record_file_)};
}

CameraResult CameraManager::StopRecord() {
  if (!recording_) {
    return {true, "not_recording",
            current_record_file_.empty() ? "" : MediaUrl(current_record_file_)};
  }
  RunCommand("kill -2 $(cat " + RecordPidPath() +
             ") 2>/dev/null; rm -f " + RecordPidPath());
  recording_ = false;
  const std::string url = current_record_file_.empty() ? "" : MediaUrl(current_record_file_);
  current_record_file_.clear();
  return {true, "record_saved", url};
}

CameraResult CameraManager::Status() const {
  return {true, streaming_ ? "streaming" : "idle", streaming_ ? "/stream/stream.m3u8" : ""};
}

bool CameraManager::EnsureDirs() const {
  return RunCommand("mkdir -p " + options_.stream_root + " " +
                    options_.media_root + "/media") == 0;
}

bool CameraManager::FileExists(const std::string& path) const {
  std::ifstream in(path.c_str());
  return in.good();
}

std::string CameraManager::MakeMediaName(const std::string& prefix,
                                         const std::string& ext) {
  const std::time_t now = std::time(nullptr);
  std::tm tm_value {};
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif
  std::ostringstream out;
  out << prefix << "_" << std::put_time(&tm_value, "%Y%m%d_%H%M%S") << "_"
      << ++media_sequence_ << "." << ext;
  return out.str();
}

std::string CameraManager::MediaPath(const std::string& filename) const {
  return options_.media_root + "/media/" + filename;
}

std::string CameraManager::MediaUrl(const std::string& filename) const {
  return filename.empty() ? "" : "/media/" + filename;
}

std::string CameraManager::StreamPlaylistPath() const {
  return options_.stream_root + "/stream.m3u8";
}

std::string CameraManager::StreamPidPath() const {
  return "/tmp/iotgw_gst_stream.pid";
}

std::string CameraManager::RecordPidPath() const {
  return "/tmp/iotgw_ffmpeg_record.pid";
}

int CameraManager::RunCommand(const std::string& command) const {
  return std::system(command.c_str());
}

}  // namespace camera
}  // namespace system_services
}  // namespace services
}  // namespace iotgw
