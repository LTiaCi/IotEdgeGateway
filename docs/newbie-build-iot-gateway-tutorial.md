# 新手从零理解并复现 IoT Edge Gateway 项目教程

这份文档是给“刚开始学，想以后自己也能写出这个项目”的你看的。

它不是只列命令，也不是只解释某一个 bug。它会按一个新手最容易理解的顺序讲：

1. 这个项目到底是什么。
2. 为什么要分成网关后端、网页前端、Qt 屏幕端、单片机端。
3. 每个目录和核心文件负责什么。
4. 数据从单片机到网页是怎么流动的。
5. 控制命令从网页或 Qt 到单片机是怎么流动的。
6. MQTT、ZigBee、SQLite、GStreamer、Mongoose、Qt 分别为什么用。
7. 常见函数、类、接口都是什么作用。
8. 如果你从零重写，应该按什么步骤来。

你可以把这个项目理解成一个“本地物联网网关”：

```text
单片机/传感器/执行器
        |
        | MQTT 或 ZigBee
        v
RK3568 网关后端 iotgw_gateway
        |
        | HTTP API / 静态网页 / 数据库 / 视频流
        v
PC 浏览器网页 + RK3568 板载 Qt 界面
```

简单说：单片机负责采集和执行，RK3568 负责集中处理，网页和 Qt 负责给人看和操作。

---

## 1. 项目最终实现了什么

当前项目主要功能有：

- 网页端实时显示温度、湿度、光照、红外状态。
- 网页端控制 LED、直流电机、蜂鸣器。
- 网页端可以选择 MQTT 或 ZigBee 控制通道。
- RK3568 后端通过 MQTT 接收单片机实时数据。
- RK3568 后端通过 MQTT 下发外设控制命令。
- RK3568 后端通过 ZigBee 透明串口收发 JSON 数据。
- 摄像头视频流可以在网页中开启、关闭、预览。
- 支持抓拍照片和视频录制。
- 支持 SQLite 数据库，保存传感器数据、控制命令、照片/视频记录、运行日志。
- RK3568 板载 Qt 界面可以横屏显示，并且开机自启。
- 开机后 WiFi、Mosquitto、网关后端、Qt 按顺序启动。

你可以把它当成一个小型毕业设计级别的“边缘网关系统”。

---

## 2. 为什么要这样分层

新手最容易犯的错误是：所有东西写在一个文件里。

这个项目没有这么做，而是分层：

```text
硬件层：
  单片机、传感器、执行器、摄像头、ZigBee 模块

通信层：
  MQTT、ZigBee 串口、HTTP

后端层：
  iotgw_gateway，负责统一接收数据、保存数据库、下发控制命令

前端层：
  www/index.html，PC 浏览器访问

屏幕层：
  qtRk，RK3568 板载屏幕显示

启动部署层：
  init.d 脚本，负责开机自启
```

这样分层的好处是：

- 网页坏了，不一定影响 MQTT。
- Qt 坏了，不一定影响网页。
- MQTT 暂时不用，也可以用 ZigBee。
- 摄像头问题可以单独排查。
- 后端 API 是中心，网页和 Qt 都调用它。

这就是工程项目里常说的“解耦”。

---

## 3. 目录结构解释

项目主目录：

```text
D:\workspace\IoTEdgeGateway
```

重要目录：

```text
config/
  配置文件，比如 MQTT 地址、HTTP 端口、摄像头路径、ZigBee 串口路径

docs/
  项目文档、协议说明、部署说明

scripts/
  打包脚本、RK3568 开机自启脚本

src/
  C++ 后端源码

third_party/mongoose/
  Mongoose 源码，用来实现 HTTP/MQTT 网络能力

www/
  Web 前端页面
```

Qt 项目在另一个目录：

```text
D:\workspace\QtProject\qtRk
```

它负责 RK3568 板载屏幕上的 Qt 控制界面。

---

## 4. 后端程序从哪里开始运行

后端入口文件是：

```text
src/gateway/main.cpp
```

核心代码：

```cpp
int main(int argc, char** argv) {
  auto args = iotgw::gateway::ParseArgs(argc, argv);
  if (args.print_version) {
    std::cout << (args.set_version.empty() ? IOTGW_VERSION : args.set_version)
              << std::endl;
    return 0;
  }
  iotgw::gateway::GatewayCore core;
  return core.Run(args);
}
```

新手解释：

- `main()` 是 C++ 程序入口。
- `ParseArgs(argc, argv)` 解析命令行参数。
- `GatewayCore core;` 创建网关核心对象。
- `core.Run(args);` 启动整个网关系统。

也就是说，后端真正的大脑在：

```text
src/gateway/gateway_core.cpp
```

---

## 5. 命令行参数 ParseArgs 是干什么的

文件：

```text
src/gateway/main.cpp
```

函数：

```cpp
GatewayArgs ParseArgs(int argc, char** argv)
```

它支持这些参数：

```text
--yaml-config <path>
--log-file <path>
--log-level <level>
--print-version
--set-version <ver>
```

为什么需要这些参数？

因为开发、板子运行、调试时配置可能不同。

例如开发电脑上可以用：

```bash
./iotgw_gateway --yaml-config config/environments/development.yaml
```

RK3568 板子上可以用：

```bash
./iotgw_gateway --yaml-config config/environments/rk3568.yaml
```

这样代码不用改，只换配置文件。

---

## 6. GatewayCore::Run 做了什么

文件：

```text
src/gateway/gateway_core.cpp
```

函数：

```cpp
int GatewayCore::Run(const GatewayArgs& args)
```

它是后端最重要的函数。

你可以把它理解成启动流程：

```text
1. 注册退出信号
2. 读取配置文件
3. 打开 SQLite 数据库
4. 创建日志系统
5. 加载传感器和执行器配置
6. 加载规则引擎
7. 初始化摄像头管理器
8. 启动 HTTP 服务器
9. 初始化 MQTT 客户端
10. 初始化 ZigBee 串口
11. 注册 API 路由
12. 进入主循环，不断 Poll MQTT、ZigBee、HTTP
```

核心主循环大概是：

```cpp
while (g_running) {
  const int64_t now = core::common::time::NowUnixMs();
  mqtt.Poll(now);
  zigbee.Poll();
  server.Poll(100);
}
```

新手解释：

- `Poll` 可以理解为“轮询一下有没有新事情发生”。
- MQTT 有没有收到消息？
- ZigBee 串口有没有收到一行 JSON？
- HTTP 有没有浏览器请求？
- 如果有，就处理。

很多嵌入式项目都是这种结构。

---

## 7. 配置文件为什么重要

配置文件在：

```text
config/environments/rk3568.yaml
config/devices/sensors.yaml
config/devices/actuators.yaml
config/rules/*.yaml
```

例如 MQTT 配置大概会包含：

```yaml
mqtt:
  enabled: true
  broker_host: "127.0.0.1"
  broker_port: 1883
  topic_prefix: "iotgw/dev/"
```

意思是：

- 后端连接本机 MQTT Broker。
- 端口是 1883。
- 主题前缀是 `iotgw/dev/`。

为什么后端连接 `127.0.0.1`？

因为 Mosquitto Broker 就跑在 RK3568 板子自己身上。

单片机连接的是：

```text
192.168.31.238:1883
```

网关后端连接的是：

```text
127.0.0.1:1883
```

它们连的是同一个 Broker，只是一个从外部连，一个从本机连。

---

## 8. 设备配置是怎么变成后端设备列表的

配置文件：

```text
config/devices/sensors.yaml
config/devices/actuators.yaml
```

相关函数：

```cpp
LoadDevicesFromConfig(cfg, registry)
LoadDevicesFromFile(...)
```

它们的作用：

```text
读取 sensors.yaml 和 actuators.yaml
把 temp、humi、light、ir、led、motor、buzzer 注册到 DeviceRegistry
```

`DeviceRegistry` 就像一个“设备花名册”。

后端以后要查：

```text
有没有 led？
led 的命令 topic 是什么？
temp 当前值是多少？
```

都从这里查。

---

## 9. MQTT 数据链路

单片机往 Broker 发数据：

```text
topic: iotgw/dev/telemetry/temp
payload: {"value":25.6}
```

网关后端订阅：

```text
iotgw/dev/telemetry/#
```

所以 temp、humi、light、ir 都能收到。

收到后走这个函数：

```cpp
auto ingest = [&](const std::string& topic, const std::string& payload) {
  ...
};
```

`ingest` 是一个 lambda 函数。

新手解释：

- lambda 就是“临时定义的函数”。
- 这里它负责统一处理所有传感器数据。

它做的事情：

```text
1. 判断是不是 telemetry 主题
2. 从 topic 里识别设备 id，比如 temp
3. 更新 DeviceRegistry 里的设备状态
4. 如果 payload 有 value，就保存到数据库
5. 触发规则引擎
6. 通过 WebSocket 广播给前端
```

所以数据链路是：

```text
单片机 publish
  -> Mosquitto
  -> MqttClient
  -> ingest()
  -> DeviceRegistry
  -> DatabaseService
  -> /api/status
  -> Web/Qt 显示
```

---

## 10. MqttClient 是什么

文件：

```text
src/core/protocol_adapters/mqtt_adapter/mqtt_adapter.hpp
src/core/protocol_adapters/mqtt_adapter/mqtt_client.cpp
```

类：

```cpp
class MqttClient
```

重要函数：

```cpp
bool Connect(const Options& opt);
bool Subscribe(const std::string& topic, uint8_t qos = 0);
bool Publish(const std::string& topic, const std::string& payload, uint8_t qos = 0, bool retain = false);
void SetMessageHandler(MessageHandler handler);
void Poll(int64_t now_ms);
bool IsOpen() const;
```

逐个解释：

### Connect

连接 MQTT Broker。

比如：

```text
mqtt://127.0.0.1:1883
```

### Subscribe

订阅主题。

比如：

```text
iotgw/dev/telemetry/#
```

### Publish

发布消息。

网页点击开灯后，后端会发布：

```text
topic: iotgw/dev/command/led
payload: {"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
```

### SetMessageHandler

设置“收到消息以后怎么办”。

本项目里设置成：

```cpp
mqtt.SetMessageHandler(ingest);
```

意思是 MQTT 收到传感器数据后，交给 `ingest` 处理。

### Poll

让 MQTT 客户端处理网络事件。

如果不 Poll，连接和收消息都不会正常推进。

---

## 11. 单片机 MQTT 协议

单片机发传感器数据：

```text
iotgw/dev/telemetry/temp   {"value":25.6}
iotgw/dev/telemetry/humi   {"value":60.2}
iotgw/dev/telemetry/light  {"value":820}
iotgw/dev/telemetry/ir     {"value":0}
```

单片机订阅控制命令：

```text
iotgw/dev/command/led
iotgw/dev/command/motor
iotgw/dev/command/buzzer
```

LED 命令：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
```

电机命令：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":30,"dir":0}}
```

蜂鸣器命令：

```json
{"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

为什么用 JSON？

因为 JSON 对人类可读，对单片机也容易解析。

---

## 12. ZigBee 是怎么接入的

ZigBee 模块是 DL-20 透明串口。

透明串口的意思是：

```text
你从串口发什么，对面就收到什么。
它不关心你里面是不是 JSON。
```

文件：

```text
src/core/protocol_adapters/zigbee_adapter/zigbee_serial_adapter.hpp
src/core/protocol_adapters/zigbee_adapter/zigbee_serial_adapter.cpp
```

类：

```cpp
class ZigbeeSerialAdapter
```

重要函数：

```cpp
bool Open(const Options& options);
void Close();
bool SendLine(const std::string& line);
void Poll();
void SetLineHandler(LineHandler handler);
```

### Open

打开串口：

```text
/dev/ttyS4
9600
8N1
无流控
```

### ConfigurePort

配置串口参数。

里面用到了 Linux 的 `termios`。

新手只需要知道：

```text
termios 是 Linux 配置串口的标准方式。
```

### SendLine

发送一行 JSON。

它会自动补 `\n`。

为什么要 `\n`？

因为我们约定“一行就是一条 JSON 消息”。

### Poll

非阻塞读取串口。

有数据就读，没有数据就返回，不会卡住整个网关。

### HandleIncomingByte

每次处理一个字节。

遇到 `\n` 就认为一条消息结束，然后调用 `line_handler_`。

---

## 13. ZigBee 数据格式

单片机通过 ZigBee 发传感器：

```json
{"type":"telemetry","id":"temp","value":26.5}
```

网关会转换成内部统一格式：

```text
topic: iotgw/dev/telemetry/temp
payload: {"value":26.5}
```

然后继续走 `ingest()`。

这很重要。

因为这样 MQTT 和 ZigBee 的数据最后都进入同一个处理入口：

```text
ingest()
```

这叫“统一入口”。

统一入口的好处是：

- 数据库不用写两套。
- 前端不用关心数据来自 MQTT 还是 ZigBee。
- 规则引擎不用写两套。

---

## 14. HTTP 服务器为什么用 Mongoose

项目使用：

```text
third_party/mongoose/mongoose.c
third_party/mongoose/mongoose.h
```

Mongoose 是一个嵌入式网络库，可以实现：

- HTTP 服务器
- WebSocket
- MQTT 客户端

为什么不用大型框架？

因为 RK3568 是嵌入式板子，不适合引入太重的后端框架。

Mongoose 的好处：

- 单文件源码，容易放进项目。
- C/C++ 都能用。
- 适合嵌入式设备。

---

## 15. API 是怎么分发的

文件：

```text
src/services/web_services/api/rest_api.hpp
```

关键函数：

```cpp
DispatchApi(method, path, body, ctx)
```

它按顺序尝试：

```cpp
HandleSystemApi
HandleDeviceApi
HandleZigbeeApi
HandleCameraApi
HandleDatabaseApi
HandleRuleApi
```

新手解释：

浏览器请求：

```text
GET /api/status
```

后端不知道这个路径该给谁处理，所以交给 `DispatchApi`。

`DispatchApi` 一个一个问：

```text
SystemApi 能处理吗？
DeviceApi 能处理吗？
ZigbeeApi 能处理吗？
...
```

谁返回不是 404，就说明谁处理了。

---

## 16. /api/status 是怎么来的

文件：

```text
src/services/web_services/api/device_api.cpp
```

代码：

```cpp
if (method == "GET" && path == "/api/status" && ctx.devices) {
  return {200, "application/json", DashboardStatusWithControl(ctx)};
}
```

意思是：

浏览器或 Qt 请求：

```text
GET /api/status
```

后端返回一个 JSON。

里面有传感器：

```json
{
  "temp": 25.6,
  "humi": 60,
  "light": 800,
  "ir": 0
}
```

也有控制状态：

```json
{
  "led_on": 1,
  "led_br": 50,
  "motor_on": 1,
  "motor_sp": 30,
  "motor_dir": 0,
  "buzzer": 0
}
```

前端和 Qt 都靠这个接口刷新界面。

---

## 17. 控制状态 ControlState 是什么

文件：

```text
src/services/web_services/api/rest_api.hpp
```

结构体：

```cpp
struct ControlState {
  int led_on = 0;
  int led_br = 50;
  int motor_on = 0;
  int motor_sp = 30;
  int motor_dir = 0;
  int buzzer = 0;
  std::mutex mu;
};
```

它保存当前外设状态。

比如：

- LED 是否开启。
- LED 亮度是多少。
- 电机是否开启。
- 电机速度是多少。
- 电机方向是什么。
- 蜂鸣器是否开启。

为什么要 `std::mutex mu`？

因为 HTTP 请求、MQTT 处理、主循环可能同时访问这些数据。

`mutex` 是锁，用来避免同时读写造成数据错乱。

---

## 18. 控制命令是怎么下发的

文件：

```text
src/services/web_services/api/device_api.cpp
```

路径：

```text
POST /api/actuators/led/set
POST /api/actuators/motor/set
POST /api/actuators/buzzer/set
```

例如网页开灯：

```json
{"on":1,"br":50}
```

后端会调用：

```cpp
NormalizeActuatorPayload(id, body)
```

把它转成统一命令：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
```

然后：

```cpp
ctx.mqtt->Publish(topic, payload, 0, false)
```

发布到 MQTT。

最后：

```cpp
UpdateActuatorControlState(id, body, ctx.control_state)
```

更新当前状态。

---

## 19. NormalizeActuatorPayload 为什么存在

如果没有这个函数，网页可能发：

```json
{"on":1,"br":50}
```

Qt 可能发：

```json
{"on":true,"br":50}
```

以后别的端可能发：

```json
{"switch":1}
```

这样单片机会很痛苦。

`NormalizeActuatorPayload` 的作用是统一格式。

统一以后，单片机只需要认一种：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
```

这就是协议设计。

---

## 20. 数据库模块

文件：

```text
src/services/storage/database_service.hpp
src/services/storage/database_service.cpp
```

类：

```cpp
class DatabaseService
```

重要函数：

```cpp
bool Open(const std::string& db_path);
void RecordSensorValue(...);
void RecordMedia(...);
void RecordCommand(...);
void RecordLog(...);
std::string RecentSensorJson(int limit);
std::string RecentMediaJson(int limit);
std::string RecentCommandsJson(int limit);
std::string RecentLogsJson(int limit);
std::string SummaryJson();
```

### Open

打开 SQLite 数据库。

如果表不存在，会创建表。

### RecordSensorValue

保存传感器数据。

例如：

```text
temp = 25.6
```

### RecordMedia

保存照片、视频文件记录。

### RecordCommand

保存外设控制命令。

比如：

```text
LED 开启，亮度 50
```

### RecordLog

保存运行日志。

### RecentXXXJson

返回最近数据，给网页数据库弹窗显示。

---

## 21. 摄像头模块

文件：

```text
src/services/system_services/camera/camera_manager.hpp
src/services/system_services/camera/camera_manager.cpp
```

类：

```cpp
class CameraManager
```

重要函数：

```cpp
bool Init(const CameraOptions& options);
CameraResult StartStream();
CameraResult StopStream();
CameraResult PreviewFrame();
CameraResult Snapshot();
CameraResult StartRecord();
CameraResult StopRecord();
CameraResult Status() const;
```

### Init

初始化摄像头配置。

例如：

```text
摄像头设备：/dev/video9
视频宽度：1280
视频高度：720
帧率：10
```

### StartStream

启动 GStreamer 管线。

它大概做：

```text
v4l2src 读取摄像头
videoconvert 转格式
mpph264enc 编码 H.264
h264parse 解析
mpegtsmux 封装 TS
hlssink 生成 HLS 切片
```

网页播放的是：

```text
/stream/stream.m3u8
```

### PreviewFrame

返回一张 JPEG 预览图。

Qt 端不用直接播放 HLS，而是定时拉：

```text
/api/camera/preview.jpg
```

因为 Qt 在 framebuffer/wayland 横屏场景下直接播 HLS 不稳定。

### Snapshot

用 ffmpeg 从 HLS 里截一张图片。

### StartRecord / StopRecord

用 ffmpeg 保存 mp4。

文件名里加时间戳，避免覆盖。

---

## 22. Web 前端

文件：

```text
www/index.html
```

它包含三部分：

```text
HTML：页面结构
CSS：页面样式
JavaScript：页面逻辑
```

主要区域：

- 顶部标题和数据库按钮。
- 左侧视频区域。
- 底部传感器卡片。
- 右侧硬件控制面板。
- 系统运行日志。
- 数据库弹窗。

### refreshStatus

网页定时请求：

```text
GET /api/status
```

然后更新：

```text
温度
湿度
光照
红外
LED 开关和亮度
电机开关、速度、方向
蜂鸣器开关
```

### sendActuatorCommand

网页控制外设时调用。

它会根据当前通信方式决定接口：

```text
MQTT:
POST /api/actuators/led/set

ZigBee:
POST /api/zigbee/actuators/led/set
```

### startStream

点击开启视频时调用：

```text
POST /api/camera/start_stream
```

然后加载：

```text
/stream/stream.m3u8
```

### addLog

在网页运行日志区域追加一行文字。

例如：

```text
LED控制 [MQTT]: {"on":1,"br":50}
```

---

## 23. Qt 项目

目录：

```text
D:\workspace\QtProject\qtRk
```

重要文件：

```text
main.cpp
widget.h
widget.cpp
qtRk.pro
start_qtRk.sh
S95qtRk
```

### qtRk.pro

Qt 工程文件。

它告诉 qmake：

```text
这个项目用哪些模块
有哪些 cpp
有哪些 h
有哪些 ui
```

### main.cpp

Qt 程序入口。

作用：

```text
创建 QApplication
判断是否需要横屏旋转
创建 Widget
全屏显示
```

为什么要旋转？

因为板子屏幕物理方向是 800x1280 竖屏，但你希望横屏使用。

`IOTGW_QT_FORCE_APP_ROTATION=1` 表示强制应用层旋转。

`IOTGW_QT_APP_ROTATION=90` 表示旋转 90 度。

### widget.h

声明界面有哪些控件和函数。

比如：

```cpp
void refreshStatus();
void startVideo();
void sendLedCommand();
QLabel *tempValue_;
QCheckBox *ledSwitch_;
QSlider *ledBrightness_;
```

新手理解：

`.h` 文件像“说明书”，告诉别人这个类有什么。

### widget.cpp

真正实现界面逻辑。

包括：

- 创建布局。
- 创建按钮。
- 绑定按钮点击事件。
- 请求后端 API。
- 更新传感器数据。
- 显示视频预览。
- 显示数据库记录。

---

## 24. Qt 里为什么用 QNetworkAccessManager

Qt 要访问后端 API，比如：

```text
http://127.0.0.1:8080/api/status
```

Qt 用：

```cpp
QNetworkAccessManager
```

发送 GET/POST 请求。

例如：

```cpp
auto *reply = network_->get(request);
connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const QByteArray body = reply->readAll();
});
```

新手解释：

- `get(request)` 发 HTTP 请求。
- `finished` 是请求完成信号。
- `connect` 是 Qt 信号槽机制。
- lambda 里处理返回结果。

---

## 25. Qt 里为什么用 QTimer

Qt 需要定时刷新状态：

```cpp
statusTimer_->start(1000);
```

意思是每 1000ms 调一次：

```cpp
refreshStatus()
```

这就是 Qt 端实时刷新的原理。

视频预览也用定时器：

```cpp
previewTimer_->start(250);
```

意思是大约每 250ms 请求一次预览图。

---

## 26. Qt 开机自启

文件：

```text
D:\workspace\QtProject\qtRk\S95qtRk
```

板子上路径：

```text
/etc/init.d/S95qtRk
```

作用：

```text
1. 等待 weston/wayland 启动
2. 等待网关 API 可访问
3. 启动 /opt/qtRk/start_qtRk.sh
```

为什么叫 `S95qtRk`？

传统 SysV init 会按文件名前面的数字顺序启动。

```text
S50launcher
S81wifi_jk
S85mosquitto
S90iotgw
S95qtRk
```

数字越大，越晚启动。

Qt 必须晚一点启动，因为：

- 显示环境要先起来。
- 网关后端要先起来。
- MQTT 最好也先起来。

---

## 27. Weston、QLauncher、kiosk-shell 是什么

RK3568 板子原厂有一个启动脚本：

```text
/etc/init.d/S50launcher
```

里面原本会启动：

```bash
weston --tty=2 --idle-time=0&
/usr/bin/QLauncher &
```

### weston

Weston 是 Wayland 显示服务器。

你可以理解成：

```text
它负责把图形界面显示到屏幕上。
```

### QLauncher

原厂桌面程序。

它会显示 qcamera、qplayer、qsetting 那些图标。

我们不需要它，所以注释掉：

```bash
#/usr/bin/QLauncher &
```

### kiosk-shell

Weston 的一种全屏 shell。

它没有桌面边栏，没有 panel，适合只跑一个全屏应用。

最终 weston 启动命令：

```bash
weston --tty=2 --idle-time=0 --shell=kiosk-shell.so&
```

---

## 28. 开机自启顺序

网关自启脚本在：

```text
scripts/init.d/
```

当前主要有：

```text
S81wifi_jk
S85mosquitto
S90iotgw
```

板子上对应：

```text
/etc/init.d/S81wifi_jk
/etc/init.d/S85mosquitto
/etc/init.d/S90iotgw
/etc/init.d/S95qtRk
```

推荐顺序：

```text
S50launcher
  启动 weston kiosk-shell

S81wifi_jk
  等 wlan0，连接 WiFi

S85mosquitto
  启动 MQTT Broker

S90iotgw
  启动网关后端

S95qtRk
  启动 Qt 横屏界面
```

---

## 29. S81wifi_jk 为什么要等 wlan0

开机时无线网卡不一定马上出现。

你之前遇到：

```text
interface wlan0 not found
```

所以脚本里要等待：

```text
/sys/class/net/wlan0
```

出现以后再连接 WiFi。

这就是嵌入式启动脚本常见问题：

```text
不是命令不对，而是执行太早。
```

---

## 30. S85mosquitto 为什么要 listener 0.0.0.0

脚本启动 Mosquitto 时写了配置：

```text
listener 1883 0.0.0.0
allow_anonymous true
```

意思：

- 监听所有网卡。
- 允许单片机从局域网连接。
- 不需要用户名密码。

如果只监听 `127.0.0.1`，网关自己能连，但单片机连不上。

---

## 31. 从零复现项目的推荐学习路线

如果你以后想自己写一个类似项目，不要一上来就写全部。

建议按这个顺序练：

### 第一步：写一个最小 HTTP 服务

目标：

```text
浏览器访问 http://板子IP:8080/
能看到 hello
```

你要学：

- C++ main 函数。
- Mongoose HTTP 监听。
- 静态文件返回。

### 第二步：写 /api/status

目标：

```json
{"temp":25.6,"humi":60}
```

浏览器能拿到 JSON。

你要学：

- HTTP GET。
- JSON 字符串。
- 前端 fetch。

### 第三步：网页显示传感器

目标：

```text
网页每 1 秒刷新温度和湿度
```

你要学：

- HTML 卡片。
- JavaScript `setInterval`。
- `fetch('/api/status')`。

### 第四步：接入 MQTT

目标：

```text
mosquitto_pub 发 {"value":26}
网页温度变化
```

你要学：

- MQTT Broker。
- topic。
- publish。
- subscribe。

### 第五步：控制 LED

目标：

```text
网页点击按钮
后端 publish 控制命令
单片机收到
```

你要学：

- HTTP POST。
- JSON body。
- MQTT publish。

### 第六步：接入数据库

目标：

```text
每次传感器数据都保存到 SQLite
网页可以查看最近 20 条
```

你要学：

- sqlite3。
- 建表。
- insert。
- select。

### 第七步：接入摄像头

目标：

```text
网页可以打开视频流
```

你要学：

- v4l2。
- GStreamer。
- HLS。
- ffmpeg。

### 第八步：做 Qt 屏幕端

目标：

```text
板子屏幕显示传感器和控制按钮
```

你要学：

- Qt Widget。
- QNetworkAccessManager。
- QTimer。
- qmake 交叉编译。

### 第九步：做开机自启

目标：

```text
重启板子后自动联网、启动 MQTT、启动网关、启动 Qt
```

你要学：

- SysV init。
- `/etc/init.d/Sxx`。
- `chmod +x`。
- 日志排查。

---

## 32. 新手必须掌握的核心概念

### 1. IP 地址

PC、RK3568、单片机必须在同一个网络。

例如：

```text
PC:      192.168.31.150
RK3568: 192.168.31.238
MCU:     192.168.31.xxx
```

### 2. 端口

```text
8080: 网页和 HTTP API
1883: MQTT
```

### 3. topic

MQTT 主题像地址。

```text
iotgw/dev/telemetry/temp
```

### 4. JSON

JSON 是数据格式。

```json
{"value":25.6}
```

### 5. 轮询

网页或 Qt 每隔一段时间请求后端。

```text
每 1 秒请求 /api/status
```

### 6. 回调

有事件发生时调用你写的函数。

比如 MQTT 收到消息后调用：

```cpp
handler_(topic, payload)
```

### 7. 非阻塞

不要让某个操作卡住整个程序。

ZigBee 串口用非阻塞读取就是这个原因。

---

## 33. 常见问题排查

### 网页打不开

检查：

```bash
ps | grep iotgw_gateway
netstat -lntp | grep 8080
cat /tmp/iotgw_gateway.log
```

### 单片机连不上 MQTT

检查：

```bash
netstat -lntp | grep 1883
cat /tmp/mosquitto.log
ifconfig wlan0
```

Mosquitto 要看到：

```text
0.0.0.0:1883
```

### 网页没有实时数据

检查：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/#' -v
```

再看：

```text
http://板子IP:8080/api/status
```

### 视频打不开

检查：

```bash
cat /tmp/iotgw_gst.log
ls /mnt/www/stream
```

### Qt 不显示

检查：

```bash
cat /tmp/qtRk.log
ps | grep -E "qtRk|weston|QLauncher"
ls -l /var/run/wayland-0
```

### Qt 边上有灰色条

检查 Weston 是否用了 kiosk-shell：

```bash
cat /etc/init.d/S50launcher
```

应包含：

```bash
weston --tty=2 --idle-time=0 --shell=kiosk-shell.so&
```

并且 QLauncher 应该被注释：

```bash
#/usr/bin/QLauncher &
```

---

## 34. 你以后自己写时的心法

### 不要一下子写完整项目

先写最小可运行版本。

例如：

```text
第一版：网页能打开
第二版：/api/status 能返回 JSON
第三版：网页能显示 JSON
第四版：MQTT 能进来
第五版：控制命令能出去
```

每一步都能跑，再加下一步。

### 每个模块都要能单独测试

例如 MQTT：

```bash
mosquitto_sub -t 'iotgw/dev/#' -v
mosquitto_pub -t iotgw/dev/telemetry/temp -m '{"value":26.5}'
```

例如 HTTP：

```bash
wget -qO- http://127.0.0.1:8080/api/status
```

例如 ZigBee：

```bash
echo '{"type":"telemetry","id":"temp","value":26.5}' > /dev/ttyS4
```

### 日志非常重要

你每次遇到问题，不要只说“不行”。

先看：

```text
/tmp/iotgw_gateway.log
/tmp/mosquitto.log
/tmp/qtRk.log
/tmp/wifi_jk.log
/tmp/iotgw_gst.log
```

日志是嵌入式开发的眼睛。

### 改代码要小步走

每次只改一个点。

例如：

```text
只改 MQTT 监听
只改 Qt 启动脚本
只改网页按钮
```

不要同时大改 5 个地方。

否则出了问题不知道是谁造成的。

---

## 35. 这个项目最核心的几条数据流

### 传感器数据流

```text
单片机
  -> MQTT publish 或 ZigBee 串口
  -> 网关 ingest()
  -> DeviceRegistry 更新状态
  -> SQLite 保存
  -> /api/status
  -> Web / Qt 显示
```

### 外设控制流

```text
Web 或 Qt 点击按钮
  -> HTTP POST /api/actuators/xxx/set
  -> 后端 NormalizeActuatorPayload
  -> MQTT publish 或 ZigBee SendLine
  -> 单片机收到 JSON
  -> 控制 LED/电机/蜂鸣器
```

### 视频数据流

```text
摄像头 /dev/video9
  -> GStreamer
  -> HLS 文件 /mnt/www/stream/stream.m3u8
  -> Web 播放
```

### Qt 预览流

```text
摄像头
  -> GStreamer tee 分一路 JPEG
  -> /tmp/iotgw_preview.jpg
  -> Qt 定时请求 /api/camera/preview.jpg
  -> QLabel 显示图片
```

### 开机启动流

```text
S50launcher
  -> weston kiosk-shell

S81wifi_jk
  -> WiFi

S85mosquitto
  -> MQTT Broker

S90iotgw
  -> 网关后端

S95qtRk
  -> Qt 屏幕界面
```

---

## 36. 如果只记住一句话

这个项目的核心思想是：

```text
把所有硬件数据和控制命令都统一进网关后端，由后端负责协议转换、状态保存、数据库记录和 API 输出，前端和 Qt 只负责展示与操作。
```

你以后自己写项目，也可以按这个思想来：

```text
硬件只管采集和执行
网关只管统一处理
界面只管展示和交互
```

这就是一个稳定项目的基本骨架。

