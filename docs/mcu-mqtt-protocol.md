# RK3568 网关与单片机 MQTT 联调协议

本文档是当前项目的最终 MQTT 对接说明，用于保证 RK3568 网关、网页前端、Mosquitto Broker、单片机程序之间的数据格式一致。

请单片机端和网关端都按本文档执行。不要临时改 topic、字段名或 payload 格式，否则网页显示和硬件控制容易出现“Broker 收到了，但网页不变”或“网页显示下发了，但单片机不执行”的问题。

## 1. 当前已验证链路

当前项目已经验证通过两条链路。

传感器上行：

```text
单片机 publish
  -> RK3568 Mosquitto Broker: 192.168.31.238:1883
  -> iotgw_gateway 订阅 iotgw/dev/telemetry/#
  -> /api/status 更新
  -> 网页实时显示温度、湿度、光照、红外
```

控制下行：

```text
网页按钮
  -> POST /api/control
  -> iotgw_gateway publish
  -> RK3568 Mosquitto Broker
  -> iotgw/dev/command/#
  -> 单片机 subscribe 后执行 LED、电机、蜂鸣器控制
```

重要变化：

- 网关现在只订阅 `iotgw/dev/telemetry/#`。
- 网关不会再订阅整个 `iotgw/dev/#`。
- `iotgw/dev/command/#` 是发给单片机的控制命令，网关不再把 command 当传感器数据处理。

这样可以避免网关把自己发出去的 command 消息又收回来，导致执行器状态混乱。

## 2. MQTT Broker 部署约定

MQTT Broker 使用 RK3568 板子上的 Mosquitto。

板子 IP 示例：

```text
192.168.31.238
```

Broker 端口：

```text
1883
```

推荐 Mosquitto 临时配置：

```conf
listener 1883 0.0.0.0
allow_anonymous true
persistence false
log_type all
```

启动命令：

```bash
cat > /tmp/iotgw-mosquitto.conf <<'EOF'
listener 1883 0.0.0.0
allow_anonymous true
persistence false
log_type all
EOF

killall mosquitto 2>/dev/null || true
mosquitto -c /tmp/iotgw-mosquitto.conf -d
```

检查监听：

```bash
netstat -an | grep 1883
```

期望看到：

```text
0.0.0.0:1883 LISTEN
```

这表示板子本机和局域网单片机都可以连接 Broker。

## 3. 网关 MQTT 配置

RK3568 网关程序 `iotgw_gateway` 作为 MQTT Client 连接本机 Broker。

配置文件：

```text
config/environments/rk3568.yaml
```

当前配置：

```yaml
mqtt:
  enabled: true
  broker_host: "127.0.0.1"
  broker_port: 1883
  client_id: iotgw-rk3568
  username: ""
  password: ""
  keepalive_sec: 30
  clean_session: true
  topic_prefix: "iotgw/dev/"
```

网关启动后应看到：

```text
[INFO] MQTT connected
[INFO] MQTT subscribed iotgw/dev/telemetry/#
```

如果看到：

```text
[WARN] MQTT connection closed
```

说明网关和 Broker 连接断开。当前网关已经加入自动重连逻辑，会定期尝试重新连接。

## 4. 单片机 MQTT 连接参数

单片机 MQTT Client 使用以下参数：

```text
Broker Host: 192.168.31.238
Broker Port: 1883
Client ID: mcu_sensor_001
Username: 空
Password: 空
QoS: 0
Retain: false
Keepalive: 30 秒
Clean Session: true
```

注意：

- 单片机连接 `192.168.31.238`，不是 `127.0.0.1`。
- `127.0.0.1` 只适用于 RK3568 板子本机进程。
- 单片机断线重连后必须重新订阅 `iotgw/dev/command/#`。

## 5. Topic 总表

### 5.1 单片机 publish 的传感器 topic

| 方向 | 传感器 | Topic |
| --- | --- | --- |
| 单片机 -> 网关 | 温度 | `iotgw/dev/telemetry/temp` |
| 单片机 -> 网关 | 湿度 | `iotgw/dev/telemetry/humi` |
| 单片机 -> 网关 | 光照 | `iotgw/dev/telemetry/light` |
| 单片机 -> 网关 | 红外 | `iotgw/dev/telemetry/ir` |

### 5.2 单片机 subscribe 的控制 topic

| 方向 | 执行器 | Topic |
| --- | --- | --- |
| 网关 -> 单片机 | LED | `iotgw/dev/command/led` |
| 网关 -> 单片机 | 电机 | `iotgw/dev/command/motor` |
| 网关 -> 单片机 | 蜂鸣器 | `iotgw/dev/command/buzzer` |

推荐单片机直接订阅：

```text
iotgw/dev/command/#
```

然后根据 topic 或 payload 里的 `device_id` 分发到对应硬件。

## 6. 传感器上报 Payload

传感器 payload 必须是标准 JSON object，并且必须包含数字字段 `value`。

正确格式：

```json
{"value":25.6}
```

错误格式：

```text
25.6
{"temp":25.6}
{"temperature":25.6}
{"value":"25.6"}
{\"value\":25.6}
```

说明：

- `value` 必须是 JSON number，不要加引号。
- 不要把反斜杠 `\` 作为 payload 内容发出去。
- C 代码字符串里可以写 `"{\"value\":25.6}"`，但 MQTT 实际发出的内容必须是 `{"value":25.6}`。

## 7. 传感器字段说明

| 设备 ID | 含义 | 单位 | 类型 | 推荐范围 | 示例 |
| --- | --- | --- | --- | --- | --- |
| `temp` | 温度 | 摄氏度 | number | `-40` 到 `125` | `{"value":25.6}` |
| `humi` | 湿度 | `%RH` | number | `0` 到 `100` | `{"value":61.9}` |
| `light` | 光照 | lux 或 ADC 原始值 | number | `0` 及以上 | `{"value":922}` |
| `ir` | 红外状态 | 状态值 | number | `0` 或 `1` | `{"value":0}` |

红外统一约定：

```text
0: 安全 / 无人 / 未触发
1: 有人 / 触发
```

当前网页判断逻辑：

```text
Number(ir) > 0 -> 有人
Number(ir) == 0 -> 安全
```

## 8. 传感器上报示例

单片机每轮可以一条一条发送，完全允许。

推荐一轮数据：

```text
topic: iotgw/dev/telemetry/temp
payload: {"value":25.6}

topic: iotgw/dev/telemetry/humi
payload: {"value":61.9}

topic: iotgw/dev/telemetry/light
payload: {"value":922}

topic: iotgw/dev/telemetry/ir
payload: {"value":0}
```

建议节奏：

```text
每 1 秒一轮：
  publish temp
  delay 20-50ms
  publish humi
  delay 20-50ms
  publish light
  delay 20-50ms
  publish ir
```

初期不要高频狂发，例如不要 10ms 一轮持续发送。

## 9. 网页状态接口

网页定时请求：

```text
GET /api/status
```

浏览器测试地址：

```text
http://192.168.31.238:8080/api/status
```

返回示例：

```json
{
  "temp": 25.6,
  "humi": 61.9,
  "light": 922,
  "ir": 0,
  "led_on": 0,
  "led_br": 50,
  "motor_on": 0,
  "motor_sp": 30,
  "motor_dir": 0,
  "buzzer": 0
}
```

其中：

- `temp`、`humi`、`light`、`ir` 来自单片机上报。
- `led_on`、`led_br`、`motor_on`、`motor_sp`、`motor_dir`、`buzzer` 是网页控制状态。

## 10. 控制命令总原则

单片机订阅：

```text
iotgw/dev/command/#
```

控制命令是“设置状态”，不是“翻转状态”。

例如收到：

```json
{"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

含义是：

```text
蜂鸣器设置为开启
```

不是：

```text
蜂鸣器状态取反
```

如果连续收到两次 `on:1`，蜂鸣器应保持开启，不要开关来回切换。

当前网页每次控制操作可能会同时发布 LED、电机、蜂鸣器三条当前状态命令。这是正常行为，单片机按收到的状态覆盖执行即可。

## 11. LED 控制命令

硬件对应关系：

```text
网页 LED 照明灯 -> STM32 A2 / PA2
```

注意：

```text
A0 / PA0 是单片机本地状态指示灯，不受网页控制。
```

Topic：

```text
iotgw/dev/command/led
```

Payload：

```json
{
  "device_id": "led",
  "type": "cmd",
  "data": {
    "on": 1,
    "br": 50
  }
}
```

字段说明：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `led` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number | `0` 关闭，`1` 开启 |
| `data.br` | number | 亮度百分比，`0` 到 `100` |

执行逻辑建议：

```text
if data.on == 0:
    关闭 LED
else:
    LED 开启
    PWM = data.br / 100 * PWM_MAX
```

## 12. 电机控制命令

硬件对应关系：

```text
PWM 调速: PA3 / TIM2_CH4
方向 AIN1: PB14
方向 AIN2: PB15
```

Topic：

```text
iotgw/dev/command/motor
```

Payload：

```json
{
  "device_id": "motor",
  "type": "cmd",
  "data": {
    "on": 1,
    "sp": 36,
    "dir": 0
  }
}
```

字段说明：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `motor` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number | `0` 停止，`1` 运行 |
| `data.sp` | number | 速度百分比，`0` 到 `100` |
| `data.dir` | number | `0` 正转，`1` 反转 |

执行逻辑建议：

```text
if data.on == 0:
    停止电机
    PWM = 0
else:
    设置方向 data.dir
    设置速度 data.sp
```

## 13. 蜂鸣器控制命令

硬件对应关系：

```text
蜂鸣器: PB3
```

Topic：

```text
iotgw/dev/command/buzzer
```

Payload：

```json
{
  "device_id": "buzzer",
  "type": "cmd",
  "data": {
    "on": 1
  }
}
```

字段说明：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `buzzer` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number 或 boolean | `0/false` 关闭，`1/true` 开启 |

单片机建议兼容：

```text
1 / true / "1" / "true" / "on"  -> 开启
0 / false / "0" / "false" / "off" -> 关闭
```

当前网页主要发送：

```text
0 / 1
```

## 14. 自动规则说明

当前为了保证网页手动控制 LED、电机、蜂鸣器稳定，项目默认关闭了自动电机规则：

```yaml
cooling_on:
  enabled: false
cooling_off:
  enabled: false
```

也就是说，正常网页控制测试时，网关不会因为温度规则自动抢占电机控制。

如果后续重新开启自动规则，网关发给电机的规则命令会使用 `data.on`：

```json
{
  "device_id": "motor",
  "type": "cmd",
  "data": {
    "on": 0
  },
  "rule_id": "cooling_off"
}
```

为兼容旧版本，单片机仍建议支持 `data.value`：

```text
优先读取 data.on
如果没有 data.on，再读取 data.value
```

## 15. 单片机推荐启动流程

```text
1. 初始化网络
2. 连接 MQTT Broker: 192.168.31.238:1883
3. 设置 client_id，例如 mcu_sensor_001
4. subscribe iotgw/dev/command/#
5. 周期读取传感器
6. publish telemetry 数据
7. mqtt_loop 持续处理收发
8. MQTT 断线后自动重连
9. 重连成功后重新 subscribe iotgw/dev/command/#
```

伪代码：

```c
mqtt_connect("192.168.31.238", 1883, "mcu_sensor_001");
mqtt_subscribe("iotgw/dev/command/#", 0);

while (1) {
    mqtt_loop();

    publish_json("iotgw/dev/telemetry/temp",  "{\"value\":25.6}");
    delay_ms(30);
    publish_json("iotgw/dev/telemetry/humi",  "{\"value\":61.9}");
    delay_ms(30);
    publish_json("iotgw/dev/telemetry/light", "{\"value\":922}");
    delay_ms(30);
    publish_json("iotgw/dev/telemetry/ir",    "{\"value\":0}");

    delay_ms(900);
}
```

## 16. 单片机命令解析伪代码

```c
void on_mqtt_message(const char *topic, const char *payload) {
    // 建议先打印，确认收到的 topic/payload 完整正确
    printf("MQTT CMD topic=%s payload=%s\n", topic, payload);

    if (strcmp(topic, "iotgw/dev/command/led") == 0) {
        int on = json_get_int(payload, "data.on", 0);
        int br = json_get_int(payload, "data.br", 50);
        led_set(on, br);
        return;
    }

    if (strcmp(topic, "iotgw/dev/command/motor") == 0) {
        int on = json_has(payload, "data.on")
                   ? json_get_int(payload, "data.on", 0)
                   : json_get_int(payload, "data.value", 0);
        int sp = json_get_int(payload, "data.sp", 30);
        int dir = json_get_int(payload, "data.dir", 0);
        motor_set(on, sp, dir);
        return;
    }

    if (strcmp(topic, "iotgw/dev/command/buzzer") == 0) {
        int on = json_get_bool_or_int(payload, "data.on", 0);
        buzzer_set(on);
        return;
    }
}
```

说明：

- 上面 `json_get_int(payload, "data.on", 0)` 是伪函数，实际要按单片机使用的 JSON 库实现。
- 如果 JSON 库不支持点路径，就先取 `data` object，再从 `data` object 里取 `on/br/sp/dir/value`。

## 17. 网关侧手动测试命令

### 17.1 监听传感器上行

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/telemetry/#' -v
```

### 17.2 模拟传感器上报

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/temp -m '{"value":26.5}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/humi -m '{"value":58}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/light -m '{"value":760}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/ir -m '{"value":1}'
```

然后打开：

```text
http://192.168.31.238:8080/api/status
```

### 17.3 监听控制下行

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/command/#' -v
```

网页上点击 LED、电机、蜂鸣器后，应看到类似：

```text
iotgw/dev/command/led {"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
iotgw/dev/command/motor {"device_id":"motor","type":"cmd","data":{"on":1,"sp":36,"dir":0}}
iotgw/dev/command/buzzer {"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

网页日志应显示：

```text
✅ 控制指令已下发到 MQTT
```

## 18. 联调排错顺序

### 18.1 网页传感器不变化

按顺序查：

1. Broker 是否监听：

```bash
netstat -an | grep 1883
```

2. 单片机数据是否到 Broker：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/telemetry/#' -v
```

3. 网关是否连接 MQTT：

```text
[INFO] MQTT connected
[INFO] MQTT subscribed iotgw/dev/telemetry/#
```

4. API 是否变化：

```text
http://192.168.31.238:8080/api/status
```

5. payload 是否是标准 JSON：

```json
{"value":25.6}
```

### 18.2 网页控制发不出去

按顺序查：

1. 网页日志是否显示：

```text
✅ 控制指令已下发到 MQTT
```

2. 监听 command：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/command/#' -v
```

3. 如果网页显示 MQTT 未连接，检查网关日志：

```text
[INFO] MQTT connected
```

4. 查看设备配置是否包含 command topic：

```text
http://192.168.31.238:8080/api/devices
```

应包含：

```text
iotgw/dev/command/led
iotgw/dev/command/motor
iotgw/dev/command/buzzer
```

### 18.3 单片机不执行控制

按顺序查：

1. 单片机是否订阅：

```text
iotgw/dev/command/#
```

2. 单片机串口是否打印收到的 topic/payload。
3. `device_id` 是否是 `led`、`motor`、`buzzer`。
4. 单片机是否把命令当成“状态设置”，而不是“状态翻转”。
5. 如果开启自动规则，单片机是否兼容 motor 的 `data.value` 旧格式。

## 19. 常见错误

### 19.1 把 command 当 telemetry 发

错误：

```text
单片机 publish iotgw/dev/command/led
```

正确：

```text
单片机 publish iotgw/dev/telemetry/temp
单片机 subscribe iotgw/dev/command/#
```

### 19.2 topic 拼错

正确：

```text
iotgw/dev/telemetry/humi
```

错误：

```text
iotgw/dev/telemetry/humidity
iotgw/dev/telemetry/hum
```

### 19.3 JSON 字段名写错

正确：

```json
{"value":25.6}
```

错误：

```json
{"temp":25.6}
```

### 19.4 控制命令当成翻转

错误理解：

```text
收到 buzzer on:1 就切换一次蜂鸣器状态
```

正确理解：

```text
收到 buzzer on:1 就保持蜂鸣器开启
收到 buzzer on:0 就保持蜂鸣器关闭
```

## 20. 最终确认清单

单片机端交付前确认：

- MQTT Host 是 `192.168.31.238`。
- MQTT Port 是 `1883`。
- Username 和 Password 为空。
- QoS 是 `0`。
- Retain 是 `false`。
- 传感器只 publish 到 `iotgw/dev/telemetry/...`。
- 控制命令只 subscribe `iotgw/dev/command/#`。
- 温度 topic 是 `iotgw/dev/telemetry/temp`。
- 湿度 topic 是 `iotgw/dev/telemetry/humi`。
- 光照 topic 是 `iotgw/dev/telemetry/light`。
- 红外 topic 是 `iotgw/dev/telemetry/ir`。
- 传感器 payload 是 `{"value":数字}`。
- 红外值优先使用 `0/1`。
- LED 解析 `data.on` 和 `data.br`。
- 电机解析 `data.on`、`data.sp`、`data.dir`。
- 电机优先解析 `data.on`，并兼容旧规则命令的 `data.value`。
- 蜂鸣器解析 `data.on`。
- 命令按“状态设置”执行，不按“翻转”执行。
- MQTT 断线后自动重连。
- 重连后重新订阅 `iotgw/dev/command/#`。
