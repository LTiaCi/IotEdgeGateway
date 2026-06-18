#pragma once

#include "core/common/logger/logger.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

struct sqlite3;

namespace iotgw {
namespace services {
namespace storage {

class DatabaseService {
 public:
  explicit DatabaseService(std::shared_ptr<core::common::log::Logger> logger);
  ~DatabaseService();

  void SetLogger(std::shared_ptr<core::common::log::Logger> logger);
  bool Open(const std::string& db_path);
  bool IsOpen() const;
  std::string DbPath() const;

  void RecordSensorValue(const std::string& device_id,
                         const std::string& topic,
                         const std::string& payload,
                         double value,
                         int64_t ts_ms);
  void RecordMedia(const std::string& media_type,
                   const std::string& url,
                   const std::string& file_path,
                   const std::string& message,
                   int64_t ts_ms);
  void RecordCommand(const std::string& device_id,
                     const std::string& topic,
                     const std::string& payload,
                     bool ok,
                     int64_t ts_ms);
  void RecordLog(const std::string& level,
                 const std::string& message,
                 int64_t ts_ms);

  std::string RecentSensorJson(int limit);
  std::string RecentMediaJson(int limit);
  std::string RecentCommandsJson(int limit);
  std::string RecentLogsJson(int limit);
  std::string SummaryJson();

 private:
  bool Exec(const std::string& sql);
  bool PrepareSchema();
  std::string QueryRowsJson(const std::string& sql);

  mutable std::mutex mu_;
  sqlite3* db_ = nullptr;
  std::string db_path_;
  std::shared_ptr<core::common::log::Logger> logger_;
};

}  // namespace storage
}  // namespace services
}  // namespace iotgw
