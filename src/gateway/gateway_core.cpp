#include "gateway/gateway_core.hpp"

#include "core/common/config/config_manager.hpp"
#include "core/common/logger/logger.hpp"
#include "core/common/utils/json_utils.hpp"
#include "core/common/utils/time_utils.hpp"
#include "core/control/rule_engine.hpp"
#include "core/device/manager/device_manager.hpp"
#include "core/device/model/device_entity.hpp"
#include "core/protocol_adapters/mqtt_adapter/mqtt_adapter.hpp"
#include "core/protocol_adapters/zigbee_adapter/zigbee_serial_adapter.hpp"
#include "services/system_services/camera/camera_manager.hpp"
#include "services/storage/database_service.hpp"
#include "services/web_services/api/rest_api.hpp"
#include "services/web_services/websocket/websocket_server.hpp"
#include "version.hpp"

#include <csignal>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace iotgw {
namespace gateway {

namespace {

using ConfigManager = core::common::config::ConfigManager;
using Logger = core::common::log::Logger;
using Level = core::common::log::Level;
using DeviceEntity = core::device::model::DeviceEntity;
using DeviceRegistry = core::device::manager::DeviceRegistry;
using MqttClient = core::protocol_adapters::mqtt_adapter::MqttClient;
using ZigbeeSerialAdapter =
    core::protocol_adapters::zigbee_adapter::ZigbeeSerialAdapter;
using MongooseServer = services::web_services::websocket::MongooseServer;
using RuleEngine = core::control::RuleEngine;
using CameraManager = services::system_services::camera::CameraManager;
using DatabaseService = services::storage::DatabaseService;
namespace json = iotgw::core::common::json;

volatile std::sig_atomic_t g_running = 1;

void HandleSignal(int) { g_running = 0; }

std::string JoinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (a.back() == '/' || a.back() == '\\') return a + b;
  return a + "/" + b;
}

std::string Trim(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::string StripQuotes(const std::string& text) {
  const std::string t = Trim(text);
  if (t.size() >= 2 &&
      ((t.front() == '"' && t.back() == '"') ||
       (t.front() == '\'' && t.back() == '\''))) {
    return t.substr(1, t.size() - 2);
  }
  return t;
}

std::string ValueAfterColon(const std::string& line) {
  const auto pos = line.find(':');
  return pos == std::string::npos ? "" : StripQuotes(line.substr(pos + 1));
}

std::shared_ptr<Logger> BuildLoggerWithDatabase(
    const ConfigManager& cfg,
    const GatewayArgs& args,
    const std::shared_ptr<DatabaseService>& database) {
  auto sinks = std::make_shared<core::common::log::MultiSink>();
  sinks->Add(std::make_shared<core::common::log::ConsoleSink>());

  const bool file_enabled = cfg.GetBoolOr("logging.file_sink_enabled", true);
  const std::string log_file =
      args.log_file.empty() ? cfg.GetStringOr("paths.log_file", "data/logs/iotgw.log")
                            : args.log_file;
  if (file_enabled && !log_file.empty()) {
    auto file_sink = std::make_shared<core::common::log::FileSink>(log_file);
    if (file_sink->IsOpen()) {
      sinks->Add(file_sink);
    }
  }
  if (database) {
    std::weak_ptr<DatabaseService> weak_database = database;
    sinks->Add(std::make_shared<core::common::log::CallbackSink>(
        [weak_database](Level level, const std::string& message) {
          if (auto database = weak_database.lock()) {
            if (database->IsOpen()) {
              database->RecordLog(core::common::log::LevelName(level), message,
                                  core::common::time::NowUnixMs());
            }
          }
        }));
  }

  auto logger = std::make_shared<Logger>(sinks);
  const std::string level = args.log_level.empty()
                                ? cfg.GetStringOr("logging.level", "info")
                                : args.log_level;
  logger->SetLevel(core::common::log::ParseLevel(level, Level::Info));
  return logger;
}

void LoadDevicesFromFile(const std::string& file_path,
                         const std::string& kind,
                         const std::string& topic_prefix,
                         DeviceRegistry& registry) {
  std::ifstream in(file_path.c_str());
  if (!in.is_open()) {
    return;
  }
  std::string line;
  std::string current_id;
  std::string protocol = "mqtt";

  auto flush = [&]() {
    if (current_id.empty()) return;
    DeviceEntity device;
    device.id = current_id;
    device.kind = kind;
    device.transport = protocol.empty() ? "mqtt" : protocol;
    device.telemetry_topic = topic_prefix + "telemetry/" + current_id;
    if (kind == "actuator") {
      device.command_topic = topic_prefix + "command/" + current_id;
    }
    registry.Register(device);
    current_id.clear();
    protocol = "mqtt";
  };

  while (std::getline(in, line)) {
    const std::string t = Trim(line);
    if (t.find("- id:") == 0) {
      flush();
      current_id = ValueAfterColon(t);
    } else if (t.find("protocol:") == 0) {
      protocol = ValueAfterColon(t);
    }
  }
  flush();
}

void LoadDevicesFromConfig(const ConfigManager& cfg, DeviceRegistry& registry) {
  const std::string root = cfg.GetStringOr("paths.config_root", "config");
  const std::string prefix = cfg.GetStringOr("mqtt.topic_prefix", "iotgw/dev/");
  LoadDevicesFromFile(JoinPath(root, "devices/sensors.yaml"), "sensor", prefix,
                      registry);
  LoadDevicesFromFile(JoinPath(root, "devices/actuators.yaml"), "actuator", prefix,
                      registry);
}

std::vector<core::control::Rule> LoadRulesFile(const std::string& file_path,
                                               const std::string& category) {
  std::ifstream in(file_path.c_str());
  std::vector<core::control::Rule> rules;
  if (!in.is_open()) {
    return rules;
  }

  core::control::Rule current;
  core::control::Action action;
  bool in_rule = false;
  bool in_action = false;
  std::string line;

  auto flush_action = [&]() {
    if (!in_action) return;
    current.then.push_back(action);
    action = core::control::Action{};
    in_action = false;
  };
  auto flush_rule = [&]() {
    if (!in_rule) return;
    flush_action();
    if (!current.id.empty()) {
      current.category = category;
      rules.push_back(current);
    }
    current = core::control::Rule{};
    in_rule = false;
  };

  while (std::getline(in, line)) {
    const std::string t = Trim(line);
    if (t.empty() || t[0] == '#') continue;
    if (t.find("- id:") == 0) {
      flush_rule();
      in_rule = true;
      current.id = ValueAfterColon(t);
    } else if (t.find("enabled:") == 0) {
      current.enabled = ValueAfterColon(t) != "false";
    } else if (t.find("sensor_id:") == 0) {
      current.when.sensor_id = ValueAfterColon(t);
    } else if (t.find("op:") == 0) {
      current.when.op = ValueAfterColon(t);
    } else if (t.find("value:") == 0 && in_action) {
      action.value = ValueAfterColon(t);
    } else if (t.find("value:") == 0) {
      try {
        current.when.value = std::stod(ValueAfterColon(t));
      } catch (...) {
        current.when.value = 0;
      }
    } else if (t.find("- type:") == 0) {
      flush_action();
      in_action = true;
      action.type = ValueAfterColon(t);
    } else if (t.find("actuator_id:") == 0) {
      action.actuator_id = ValueAfterColon(t);
    } else if (t.find("level:") == 0) {
      action.level = ValueAfterColon(t);
    } else if (t.find("message:") == 0) {
      action.message = ValueAfterColon(t);
    }
  }
  flush_rule();
  return rules;
}

bool LoadRulesFromConfig(const ConfigManager& cfg, RuleEngine& engine) {
  const std::string root = cfg.GetStringOr("paths.config_root", "config");
  engine.Clear();
  engine.AddRules(
      LoadRulesFile(JoinPath(root, "rules/automation-rules.yaml"), "automation"));
  engine.AddRules(LoadRulesFile(JoinPath(root, "rules/alarm-rules.yaml"), "alarm"));
  return true;
}

bool TryParseSensorValue(const std::string& payload, double& out) {
  if (json::GetJsonNumber(payload, "value", out)) {
    return true;
  }
  try {
    out = std::stod(Trim(payload));
    return true;
  } catch (...) {
    return false;
  }
}

bool IsTelemetryTopic(const std::string& topic) {
  return topic.find("/telemetry/") != std::string::npos;
}

bool TryParseZigbeeTelemetry(const std::string& line,
                             std::string& topic,
                             std::string& payload,
                             const std::string& topic_prefix) {
  const std::string type = json::GetJsonString(line, "type");
  const std::string id = json::GetJsonString(line, "id");
  double value = 0.0;
  if (type != "telemetry" || id.empty() ||
      !json::GetJsonNumber(line, "value", value)) {
    return false;
  }
  topic = topic_prefix + "telemetry/" + id;
  payload = json::Object({{"value", json::Number(value)}});
  return true;
}

void LogRuleAction(const std::shared_ptr<Logger>& logger,
                   const core::control::Action& action) {
  if (!logger) return;
  const std::string message = action.message.empty() ? "Rule action triggered"
                                                    : action.message;
  const auto level = core::common::log::ParseLevel(action.level, Level::Warn);
  logger->Log(level, message);
}

}  // namespace

int GatewayCore::Run(const GatewayArgs& args) {
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);
  g_running = 1;

  ConfigManager cfg;
  if (!cfg.LoadYamlFile(args.yaml_config)) {
    std::cerr << "Failed to load config: " << args.yaml_config << std::endl;
    return 2;
  }

  auto database = std::make_shared<DatabaseService>(nullptr);
  const std::string data_dir = cfg.GetStringOr("paths.data_dir", "data");
  database->Open(cfg.GetStringOr("database.path", JoinPath(data_dir, "iotgw.db")));

  auto logger = BuildLoggerWithDatabase(cfg, args, database);
  database->SetLogger(logger);
  const std::string version =
      args.set_version.empty() ? std::string(IOTGW_VERSION) : args.set_version;
  logger->Info("Starting IoT Edge Gateway " + version);

  DeviceRegistry registry;
  LoadDevicesFromConfig(cfg, registry);

  RuleEngine rules;
  LoadRulesFromConfig(cfg, rules);

  CameraManager camera(logger);
  services::system_services::camera::CameraOptions camera_options;
  camera_options.device = cfg.GetStringOr("camera.device", "/dev/video9");
  camera_options.stream_root =
      cfg.GetStringOr("camera.stream_root", "/mnt/www/stream");
  camera_options.media_root =
      cfg.GetStringOr("camera.media_root", "/userdata/www");
  camera_options.width = static_cast<int>(cfg.GetInt64Or("camera.width", 1280));
  camera_options.height = static_cast<int>(cfg.GetInt64Or("camera.height", 720));
  camera_options.fps = static_cast<int>(cfg.GetInt64Or("camera.fps", 10));
  camera.Init(camera_options);

  MongooseServer server;
  int64_t port = cfg.GetInt64Or("network.http_api.port", 8080);
  const std::string host = cfg.GetStringOr("network.http_api.host", "0.0.0.0");
  const std::string listen_addr =
      "http://" + host + ":" + std::to_string(port);

  MongooseServer::Options options;
  options.listen_addr = listen_addr;
  options.ws_path = cfg.GetStringOr("network.websocket.path", "/ws");
  options.www_root = cfg.GetStringOr("paths.www_root", "www");
  options.stream_www_root =
      cfg.GetStringOr("camera.stream_www_root", "/mnt/www");
  options.stream_root = camera_options.stream_root;
  options.media_root = camera_options.media_root;

  if (!server.Start(options)) {
    logger->Error("Failed to start web server at " + listen_addr);
    return 3;
  }
  logger->Info("Web server listening at " + listen_addr);

  MqttClient mqtt(server.GetMgr(), logger);

  auto ingest = [&](const std::string& topic, const std::string& payload) {
    if (!IsTelemetryTopic(topic)) {
      logger->Info("Telemetry non-telemetry ignored topic=" + topic +
                   " payload=" + payload);
      return;
    }

    const int64_t now = core::common::time::NowUnixMs();
    std::string device_id;
    registry.UpsertMqttDeviceFromTopic(topic, payload, now, device_id);
    if (!device_id.empty()) {
      logger->Info("Telemetry device=" + device_id + " topic=" + topic +
                   " payload=" + payload);
    } else {
      logger->Warn("Telemetry ignored topic=" + topic + " payload=" +
                   payload);
    }

    double sensor_value = 0.0;
    if (!device_id.empty() && TryParseSensorValue(payload, sensor_value)) {
      if (database && database->IsOpen()) {
        database->RecordSensorValue(device_id, topic, payload, sensor_value, now);
      }
      rules.OnSensorValue(device_id, sensor_value,
                          [&](const core::control::Rule& rule,
                              const core::control::Action& action) {
                            if (action.type == "actuator_set") {
                              std::string cmd_topic;
                              if (registry.GetCommandTopic(action.actuator_id,
                                                           cmd_topic)) {
                                const std::string data_key =
                                    action.actuator_id == "motor" ? "on" : "value";
                                const std::string cmd = json::Object({
                                    {"device_id", json::Quote(action.actuator_id)},
                                    {"type", json::Quote("cmd")},
                                    {"data", json::Object({{data_key, action.value}})},
                                    {"rule_id", json::Quote(rule.id)},
                                });
                                if (!mqtt.Publish(cmd_topic, cmd, 0, false)) {
                                  logger->Warn("MQTT not connected for rule " +
                                               rule.id);
                                }
                              }
                            } else if (action.type == "log") {
                              LogRuleAction(logger, action);
                            }
                          });
    }

    const std::string frame = json::Object({
        {"type", json::Quote("mqtt_msg")},
        {"topic", json::Quote(topic)},
        {"payload", json::Quote(payload)},
        {"device_id", json::Quote(device_id)},
        {"ts", json::Number(now)},
    });
    server.BroadcastText(frame);
  };

  mqtt.SetMessageHandler(ingest);

  ZigbeeSerialAdapter zigbee(logger);
  const bool zigbee_enabled = cfg.GetBoolOr("zigbee.enabled", false);
  const std::string zigbee_topic_prefix =
      cfg.GetStringOr("mqtt.topic_prefix", "iotgw/dev/");
  int64_t last_zigbee_ignored_warn_ms = 0;
  if (zigbee_enabled) {
    ZigbeeSerialAdapter::Options zigbee_options;
    zigbee_options.device = cfg.GetStringOr("zigbee.device", "/dev/ttyS4");
    zigbee_options.baudrate =
        static_cast<int>(cfg.GetInt64Or("zigbee.baudrate", 9600));
    zigbee.SetLineHandler([&](const std::string& line) {
      std::string topic;
      std::string payload;
      if (TryParseZigbeeTelemetry(line, topic, payload, zigbee_topic_prefix)) {
        logger->Info("ZigBee telemetry topic=" + topic + " payload=" + payload);
        ingest(topic, payload);
      } else {
        const int64_t now = core::common::time::NowUnixMs();
        if (now - last_zigbee_ignored_warn_ms >= 3000) {
          logger->Warn("ZigBee line ignored len=" + std::to_string(line.size()) +
                       " content=" + line);
          last_zigbee_ignored_warn_ms = now;
        }
      }
    });
    zigbee.Open(zigbee_options);
  } else {
    logger->Info("ZigBee disabled by config");
  }

  const bool mqtt_enabled = cfg.GetBoolOr("mqtt.enabled", false);
  if (mqtt_enabled) {
    MqttClient::Options mqtt_options;
    const std::string host_cfg = cfg.GetStringOr("mqtt.broker_host", "127.0.0.1");
    const int64_t mqtt_port = cfg.GetInt64Or("mqtt.broker_port", 1883);
    mqtt_options.url = "mqtt://" + host_cfg + ":" + std::to_string(mqtt_port);
    mqtt_options.client_id = cfg.GetStringOr("mqtt.client_id", "iotgw");
    mqtt_options.user = cfg.GetStringOr("mqtt.username", "");
    mqtt_options.pass = cfg.GetStringOr("mqtt.password", "");
    mqtt_options.keepalive_sec =
        static_cast<uint16_t>(cfg.GetInt64Or("mqtt.keepalive_sec", 30));
    mqtt_options.clean_session = cfg.GetBoolOr("mqtt.clean_session", true);
    mqtt.Connect(mqtt_options);
    mqtt.Subscribe(cfg.GetStringOr("mqtt.topic_prefix", "iotgw/dev/") +
                       "telemetry/#",
                   0);
  } else {
    logger->Info("MQTT disabled by config; WebSocket simulation remains available");
  }

  services::web_services::api::ControlState control_state;
  services::web_services::api::ApiContext api_ctx;
  api_ctx.devices = &registry;
  api_ctx.rules = &rules;
  api_ctx.mqtt = &mqtt;
  api_ctx.zigbee = &zigbee;
  api_ctx.camera = &camera;
  api_ctx.database = database.get();
  api_ctx.control_state = &control_state;
  api_ctx.version = version;
  api_ctx.ingest_telemetry = ingest;
  api_ctx.reload_rules = [&]() { return LoadRulesFromConfig(cfg, rules); };

  server.SetHttpHandler([&](const std::string& method,
                            const std::string& uri,
                            const std::string& body,
                            std::string& content_type,
                            int& status,
                            std::string& response) {
    if (uri.find("/api/") != 0) {
      return false;
    }
    const auto api_response =
        services::web_services::api::DispatchApi(method, uri, body, api_ctx);
    content_type = api_response.content_type;
    status = api_response.status;
    response = api_response.body;
    return true;
  });

  server.SetWsMessageHandler([&](const std::string& text, MongooseServer& ws) {
    const std::string topic = json::GetJsonString(text, "topic");
    const std::string payload = json::GetJsonString(text, "payload");
    if (topic.empty()) {
      ws.BroadcastText(json::Object({{"type", json::Quote("error")},
                                     {"error", json::Quote("missing_topic")}}));
      return;
    }
    ingest(topic, payload.empty() ? text : payload);
    ws.BroadcastText(json::Object({{"type", json::Quote("mqtt_pub_ack")},
                                   {"ok", "true"}}));
  });

  int64_t last_heartbeat = core::common::time::NowUnixMs();
  while (g_running) {
    server.Poll(50);
    const int64_t now = core::common::time::NowUnixMs();
    mqtt.Poll(now);
    zigbee.Poll();
    if (now - last_heartbeat >= 10000) {
      logger->Info("heartbeat devices=" + std::to_string(registry.List().size()) +
                   " rules=" + std::to_string(rules.Rules().size()));
      last_heartbeat = now;
    }
  }

  logger->Info("Gateway stopping");
  logger->Flush();
  return 0;
}

}  // namespace gateway
}  // namespace iotgw
