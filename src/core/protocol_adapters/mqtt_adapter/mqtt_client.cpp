#include "core/protocol_adapters/mqtt_adapter/mqtt_adapter.hpp"

#include "core/common/utils/time_utils.hpp"
#include "mongoose.h"

namespace iotgw {
namespace core {
namespace protocol_adapters {
namespace mqtt_adapter {

namespace {

const char* MgStrData(const struct mg_str& s) {
  return s.buf;
}

}  // namespace

MqttClient::MqttClient(struct mg_mgr* mgr,
                       std::shared_ptr<core::common::log::Logger> logger)
    : mgr_(mgr), logger_(std::move(logger)) {}

bool MqttClient::Connect(const Options& opt) {
  if (mgr_ == nullptr) {
    return false;
  }
  options_ = opt;
  reconnect_enabled_ = true;
  next_reconnect_ms_ = 0;
  if (conn_ != nullptr || open_) {
    return true;
  }
  struct mg_mqtt_opts mqtt_opts = {};
  mqtt_opts.user = mg_str(opt.user.c_str());
  mqtt_opts.pass = mg_str(opt.pass.c_str());
  mqtt_opts.client_id = mg_str(opt.client_id.c_str());
  mqtt_opts.version = opt.version;
  mqtt_opts.keepalive = opt.keepalive_sec;
  mqtt_opts.clean = opt.clean_session;
  conn_ = mg_mqtt_connect(
      mgr_, opt.url.c_str(), &mqtt_opts,
      [](struct mg_connection* c, int ev, void* ev_data) {
        auto* self = static_cast<MqttClient*>(c->fn_data);
        if (self) {
          self->OnMongooseEvent(c, ev, ev_data);
        }
      },
      this);
  if (conn_ == nullptr) {
    if (logger_) logger_->Warn("MQTT connect request failed");
    next_reconnect_ms_ =
        core::common::time::NowUnixMs() + static_cast<int64_t>(3000);
    return false;
  }
  return true;
}

bool MqttClient::Subscribe(const std::string& topic, uint8_t qos) {
  subscriptions_.push_back({topic, qos});
  if (conn_ == nullptr) {
    return false;
  }
  if (!open_) {
    return true;
  }
  struct mg_mqtt_opts opts = {};
  opts.topic = mg_str(topic.c_str());
  opts.qos = qos;
  mg_mqtt_sub(conn_, &opts);
  return true;
}

bool MqttClient::Publish(const std::string& topic,
                         const std::string& payload,
                         uint8_t qos,
                         bool retain) {
  if (conn_ == nullptr || !open_) {
    return false;
  }
  struct mg_mqtt_opts opts = {};
  opts.topic = mg_str(topic.c_str());
  opts.message = mg_str(payload.c_str());
  opts.qos = qos;
  opts.retain = retain;
  opts.retransmit_id = 0;
  next_id_ = mg_mqtt_pub(conn_, &opts);
  return true;
}

void MqttClient::SetMessageHandler(MessageHandler handler) {
  handler_ = std::move(handler);
}

bool MqttClient::IsOpen() const { return open_; }

void MqttClient::Poll(int64_t now_ms) {
  if (!reconnect_enabled_ || conn_ != nullptr || open_) {
    return;
  }
  if (now_ms < next_reconnect_ms_) {
    return;
  }
  next_reconnect_ms_ = now_ms + static_cast<int64_t>(3000);
  if (logger_) logger_->Info("MQTT reconnecting");
  const Options opt = options_;
  Connect(opt);
}

void MqttClient::OnMongooseEvent(struct mg_connection* c, int ev, void* ev_data) {
  if (ev == MG_EV_MQTT_OPEN) {
    open_ = true;
    if (logger_) logger_->Info("MQTT connected");
    for (const auto& sub : subscriptions_) {
      struct mg_mqtt_opts opts = {};
      opts.topic = mg_str(sub.first.c_str());
      opts.qos = sub.second;
      mg_mqtt_sub(conn_, &opts);
      if (logger_) logger_->Info("MQTT subscribed " + sub.first);
    }
  } else if (ev == MG_EV_MQTT_MSG) {
    auto* mm = static_cast<struct mg_mqtt_message*>(ev_data);
    if (handler_ && mm) {
      handler_(std::string(MgStrData(mm->topic), mm->topic.len),
               std::string(MgStrData(mm->data), mm->data.len));
    }
  } else if (ev == MG_EV_CLOSE && c == conn_) {
    open_ = false;
    conn_ = nullptr;
    next_reconnect_ms_ =
        core::common::time::NowUnixMs() + static_cast<int64_t>(3000);
    if (logger_) logger_->Warn("MQTT connection closed");
  }
}

}  // namespace mqtt_adapter
}  // namespace protocol_adapters
}  // namespace core
}  // namespace iotgw
