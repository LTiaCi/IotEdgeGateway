#pragma once

#include "core/common/utils/time_utils.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace iotgw {
namespace core {
namespace common {
namespace log {

enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5 };

inline std::string LevelName(Level level) {
  switch (level) {
    case Level::Trace:
      return "TRACE";
    case Level::Debug:
      return "DEBUG";
    case Level::Info:
      return "INFO";
    case Level::Warn:
      return "WARN";
    case Level::Error:
      return "ERROR";
    case Level::Fatal:
      return "FATAL";
  }
  return "INFO";
}

inline Level ParseLevel(const std::string& text, Level fallback = Level::Info) {
  if (text == "trace" || text == "TRACE") return Level::Trace;
  if (text == "debug" || text == "DEBUG") return Level::Debug;
  if (text == "info" || text == "INFO") return Level::Info;
  if (text == "warn" || text == "WARN") return Level::Warn;
  if (text == "error" || text == "ERROR") return Level::Error;
  if (text == "fatal" || text == "FATAL") return Level::Fatal;
  return fallback;
}

class Sink {
 public:
  virtual ~Sink() = default;
  virtual void Write(Level level, const std::string& message) = 0;
  virtual void Flush() = 0;
};

class ConsoleSink : public Sink {
 public:
  void Write(Level level, const std::string& message) override {
    std::lock_guard<std::mutex> lock(mu_);
    std::ostream& os = level >= Level::Error ? std::cerr : std::cout;
    os << common::time::NowLocalText() << " [" << LevelName(level) << "] "
       << message << std::endl;
  }

  void Flush() override {
    std::cout.flush();
    std::cerr.flush();
  }

 private:
  std::mutex mu_;
};

class FileSink : public Sink {
 public:
  explicit FileSink(const std::string& path);
  bool IsOpen() const;
  void Write(Level level, const std::string& message) override;
  void Flush() override;

 private:
  mutable std::mutex mu_;
  std::ofstream out_;
};

class MultiSink : public Sink {
 public:
  void Add(std::shared_ptr<Sink> sink) { sinks_.push_back(sink); }

  void Write(Level level, const std::string& message) override {
    for (auto& sink : sinks_) {
      if (sink) {
        sink->Write(level, message);
      }
    }
  }

  void Flush() override {
    for (auto& sink : sinks_) {
      if (sink) {
        sink->Flush();
      }
    }
  }

 private:
  std::vector<std::shared_ptr<Sink>> sinks_;
};

class Logger {
 public:
  explicit Logger(std::shared_ptr<Sink> sink) : sink_(std::move(sink)) {}

  void SetLevel(Level level) {
    std::lock_guard<std::mutex> lock(mu_);
    level_ = level;
  }

  Level GetLevel() const {
    std::lock_guard<std::mutex> lock(mu_);
    return level_;
  }

  void Log(Level level, const std::string& msg) {
    std::shared_ptr<Sink> sink;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (level < level_) {
        return;
      }
      sink = sink_;
    }
    if (sink) {
      sink->Write(level, msg);
    }
  }

  void Log(Level level, const std::string& tag, const std::string& msg) {
    Log(level, "[" + tag + "] " + msg);
  }

  void Trace(const std::string& msg) { Log(Level::Trace, msg); }
  void Debug(const std::string& msg) { Log(Level::Debug, msg); }
  void Info(const std::string& msg) { Log(Level::Info, msg); }
  void Warn(const std::string& msg) { Log(Level::Warn, msg); }
  void Error(const std::string& msg) { Log(Level::Error, msg); }
  void Fatal(const std::string& msg) { Log(Level::Fatal, msg); }

  void Flush() {
    std::shared_ptr<Sink> sink;
    {
      std::lock_guard<std::mutex> lock(mu_);
      sink = sink_;
    }
    if (sink) {
      sink->Flush();
    }
  }

 private:
  mutable std::mutex mu_;
  Level level_ = Level::Info;
  std::shared_ptr<Sink> sink_;
};

}  // namespace log
}  // namespace common
}  // namespace core
}  // namespace iotgw
