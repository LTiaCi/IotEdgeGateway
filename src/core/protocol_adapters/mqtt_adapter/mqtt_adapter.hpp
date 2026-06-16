#pragma once

#include "core/common/logger/logger.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct mg_mgr;
struct mg_connection;

namespace iotgw {
namespace core {
namespace protocol_adapters {
namespace mqtt_adapter {

class MqttClient {
 public:
  struct Options {
    std::string url;
    std::string client_id;
    std::string user;
    std::string pass;
    uint16_t keepalive_sec = 30;
    bool clean_session = true;
    uint8_t version = 4;
  };

  using MessageHandler =
      std::function<void(const std::string& topic, const std::string& payload)>;

  MqttClient(struct mg_mgr* mgr,
             std::shared_ptr<core::common::log::Logger> logger);

  bool Connect(const Options& opt);
  bool Subscribe(const std::string& topic, uint8_t qos = 0);
  bool Publish(const std::string& topic,
               const std::string& payload,
               uint8_t qos = 0,
               bool retain = false);
  void SetMessageHandler(MessageHandler handler);
  bool IsOpen() const;
  void OnMongooseEvent(struct mg_connection* c, int ev, void* ev_data);

 private:
  struct mg_mgr* mgr_ = nullptr;
  struct mg_connection* conn_ = nullptr;
  bool open_ = false;
  uint16_t next_id_ = 1;
  MessageHandler handler_;
  std::shared_ptr<core::common::log::Logger> logger_;
};

}  // namespace mqtt_adapter
}  // namespace protocol_adapters
}  // namespace core
}  // namespace iotgw
