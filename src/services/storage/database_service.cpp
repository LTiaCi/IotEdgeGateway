#include "services/storage/database_service.hpp"

#include "core/common/utils/json_utils.hpp"

#include <sqlite3.h>

#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#endif

namespace iotgw {
namespace services {
namespace storage {

namespace {

namespace json = iotgw::core::common::json;

bool MakeDir(const std::string& path) {
  if (path.empty()) {
    return true;
  }
#if defined(_WIN32)
  return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
  return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
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

void BindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

int ClampLimit(int limit) {
  if (limit <= 0) {
    return 50;
  }
  if (limit > 500) {
    return 500;
  }
  return limit;
}

std::string ColumnValueJson(sqlite3_stmt* stmt, int col) {
  const int type = sqlite3_column_type(stmt, col);
  switch (type) {
    case SQLITE_INTEGER:
      return json::Number(sqlite3_column_int64(stmt, col));
    case SQLITE_FLOAT:
      return json::Number(sqlite3_column_double(stmt, col));
    case SQLITE_NULL:
      return "null";
    case SQLITE_TEXT:
    default: {
      const unsigned char* text = sqlite3_column_text(stmt, col);
      return json::Quote(text ? reinterpret_cast<const char*>(text) : "");
    }
  }
}

}  // namespace

DatabaseService::DatabaseService(
    std::shared_ptr<core::common::log::Logger> logger)
    : logger_(std::move(logger)) {}

DatabaseService::~DatabaseService() {
  std::lock_guard<std::mutex> lock(mu_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void DatabaseService::SetLogger(
    std::shared_ptr<core::common::log::Logger> logger) {
  std::lock_guard<std::mutex> lock(mu_);
  logger_ = std::move(logger);
}

bool DatabaseService::Open(const std::string& db_path) {
  std::lock_guard<std::mutex> lock(mu_);
  if (db_) {
    return true;
  }

  EnsureParentDirs(db_path);
  if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
    if (logger_) {
      logger_->Warn("SQLite open failed: " + db_path);
    }
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return false;
  }

  db_path_ = db_path;
  Exec("PRAGMA journal_mode=WAL;");
  Exec("PRAGMA synchronous=NORMAL;");
  Exec("PRAGMA busy_timeout=3000;");
  if (!PrepareSchema()) {
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  if (logger_) {
    logger_->Info("SQLite database opened at " + db_path);
  }
  return true;
}

bool DatabaseService::IsOpen() const {
  std::lock_guard<std::mutex> lock(mu_);
  return db_ != nullptr;
}

std::string DatabaseService::DbPath() const {
  std::lock_guard<std::mutex> lock(mu_);
  return db_path_;
}

bool DatabaseService::Exec(const std::string& sql) {
  if (!db_) {
    return false;
  }
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    if (logger_) {
      logger_->Warn("SQLite exec failed: " + std::string(err ? err : "unknown"));
    }
    sqlite3_free(err);
    return false;
  }
  return true;
}

bool DatabaseService::PrepareSchema() {
  return Exec(
             "CREATE TABLE IF NOT EXISTS sensor_samples ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "ts_ms INTEGER NOT NULL,"
             "device_id TEXT NOT NULL,"
             "topic TEXT,"
             "value REAL,"
             "payload TEXT"
             ");") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_sensor_ts ON sensor_samples(ts_ms);") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_sensor_device ON sensor_samples(device_id, ts_ms);") &&
         Exec(
             "CREATE TABLE IF NOT EXISTS media_files ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "ts_ms INTEGER NOT NULL,"
             "media_type TEXT NOT NULL,"
             "url TEXT,"
             "file_path TEXT,"
             "message TEXT"
             ");") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_media_ts ON media_files(ts_ms);") &&
         Exec(
             "CREATE TABLE IF NOT EXISTS actuator_commands ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "ts_ms INTEGER NOT NULL,"
             "device_id TEXT NOT NULL,"
             "topic TEXT,"
             "payload TEXT,"
             "ok INTEGER"
             ");") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_command_ts ON actuator_commands(ts_ms);") &&
         Exec(
             "CREATE TABLE IF NOT EXISTS runtime_logs ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "ts_ms INTEGER NOT NULL,"
             "level TEXT NOT NULL,"
             "message TEXT NOT NULL"
             ");") &&
         Exec("CREATE INDEX IF NOT EXISTS idx_logs_ts ON runtime_logs(ts_ms);");
}

void DatabaseService::RecordSensorValue(const std::string& device_id,
                                        const std::string& topic,
                                        const std::string& payload,
                                        double value,
                                        int64_t ts_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO sensor_samples(ts_ms,device_id,topic,value,payload) "
      "VALUES(?,?,?,?,?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
  sqlite3_bind_int64(stmt, 1, ts_ms);
  BindText(stmt, 2, device_id);
  BindText(stmt, 3, topic);
  sqlite3_bind_double(stmt, 4, value);
  BindText(stmt, 5, payload);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void DatabaseService::RecordMedia(const std::string& media_type,
                                  const std::string& url,
                                  const std::string& file_path,
                                  const std::string& message,
                                  int64_t ts_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO media_files(ts_ms,media_type,url,file_path,message) "
      "VALUES(?,?,?,?,?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
  sqlite3_bind_int64(stmt, 1, ts_ms);
  BindText(stmt, 2, media_type);
  BindText(stmt, 3, url);
  BindText(stmt, 4, file_path);
  BindText(stmt, 5, message);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void DatabaseService::RecordCommand(const std::string& device_id,
                                    const std::string& topic,
                                    const std::string& payload,
                                    bool ok,
                                    int64_t ts_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO actuator_commands(ts_ms,device_id,topic,payload,ok) "
      "VALUES(?,?,?,?,?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
  sqlite3_bind_int64(stmt, 1, ts_ms);
  BindText(stmt, 2, device_id);
  BindText(stmt, 3, topic);
  BindText(stmt, 4, payload);
  sqlite3_bind_int(stmt, 5, ok ? 1 : 0);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void DatabaseService::RecordLog(const std::string& level,
                                const std::string& message,
                                int64_t ts_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) return;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO runtime_logs(ts_ms,level,message) VALUES(?,?,?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
  sqlite3_bind_int64(stmt, 1, ts_ms);
  BindText(stmt, 2, level);
  BindText(stmt, 3, message);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  sqlite3_exec(db_,
               "DELETE FROM runtime_logs WHERE id NOT IN "
               "(SELECT id FROM runtime_logs ORDER BY ts_ms DESC LIMIT 2000);",
               nullptr, nullptr, nullptr);
}

std::string DatabaseService::QueryRowsJson(const std::string& sql) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) {
    return "[]";
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return "[]";
  }
  std::vector<std::string> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    std::ostringstream row;
    row << "{";
    const int cols = sqlite3_column_count(stmt);
    for (int i = 0; i < cols; ++i) {
      if (i != 0) {
        row << ",";
      }
      row << json::Quote(sqlite3_column_name(stmt, i)) << ":"
          << ColumnValueJson(stmt, i);
    }
    row << "}";
    rows.push_back(row.str());
  }
  sqlite3_finalize(stmt);
  return json::Array(rows);
}

std::string DatabaseService::RecentSensorJson(int limit) {
  std::ostringstream sql;
  sql << "SELECT id,ts_ms,device_id,topic,value,payload FROM sensor_samples "
      << "ORDER BY ts_ms DESC LIMIT " << ClampLimit(limit) << ";";
  return QueryRowsJson(sql.str());
}

std::string DatabaseService::RecentMediaJson(int limit) {
  std::ostringstream sql;
  sql << "SELECT id,ts_ms,media_type,url,file_path,message FROM media_files "
      << "ORDER BY ts_ms DESC LIMIT " << ClampLimit(limit) << ";";
  return QueryRowsJson(sql.str());
}

std::string DatabaseService::RecentCommandsJson(int limit) {
  std::ostringstream sql;
  sql << "SELECT id,ts_ms,device_id,topic,payload,ok FROM actuator_commands "
      << "ORDER BY ts_ms DESC LIMIT " << ClampLimit(limit) << ";";
  return QueryRowsJson(sql.str());
}

std::string DatabaseService::RecentLogsJson(int limit) {
  std::ostringstream sql;
  sql << "SELECT id,ts_ms,level,message FROM runtime_logs "
      << "ORDER BY ts_ms DESC LIMIT " << ClampLimit(limit) << ";";
  return QueryRowsJson(sql.str());
}

std::string DatabaseService::SummaryJson() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!db_) {
    return json::Object({{"open", "false"}});
  }
  auto count_table = [&](const std::string& table) -> int64_t {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "SELECT COUNT(*) FROM " + table + ";";
    int64_t count = 0;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
  };
  return json::Object({
      {"open", "true"},
      {"path", json::Quote(db_path_)},
      {"sensor_samples", json::Number(count_table("sensor_samples"))},
      {"media_files", json::Number(count_table("media_files"))},
      {"actuator_commands", json::Number(count_table("actuator_commands"))},
      {"runtime_logs", json::Number(count_table("runtime_logs"))},
  });
}

}  // namespace storage
}  // namespace services
}  // namespace iotgw
