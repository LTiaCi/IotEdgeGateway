# RK3568 网关到 STM32 外设控制 MQTT 对接规范

本文档用于 STM32 端对接网页右侧“硬件外设控制”功能，覆盖：

- LED 照明灯
- 直流电机控制
- 紧急蜂鸣报警

当前已经确认：RK3568 板子上使用 `mosquitto_sub` 能监听到网页操作后网关发出的 JSON 命令。

因此，STM32 端请重点确认：

1. 是否连接到正确的 MQTT Broker。
2. 是否订阅了正确的控制 topic。
3. 是否完整收到 topic 和 payload。
4. 是否按嵌套 JSON 的 `data.xxx` 字段解析。
5. GPIO、PWM、电机驱动、蜂鸣器电平逻辑是否和命令语义一致。

本文档写的是当前项目真实发送格式，不包含未实现字段。当前控制 MQTT payload 中没有 `ts` 字段。

## 1. 网络与角色

### 1.1 RK3568 网关

```text
RK3568 IP: 192.168.31.238
Web 页面: http://192.168.31.238:8080/
MQTT Broker: Mosquitto
Broker IP: 192.168.31.238
Broker Port: 1883
```

网关程序 `iotgw_gateway` 自己连接本机 Broker：

```text
127.0.0.1:1883
```

STM32 不是连接 `127.0.0.1`，STM32 应连接 RK3568 在局域网里的 IP：

```text
192.168.31.238:1883
```

### 1.2 STM32 MQTT 连接参数

```text
MQTT Host: 192.168.31.238
MQTT Port: 1883
Client ID: stm32_actuator_001
Username: 空
Password: 空
QoS: 0
Retain: false
Clean session: true
Keepalive: 建议 30 秒
```

Client ID 可以自定义，但不要和 RK3568 网关的 `iotgw-rk3568` 重复，也不要多个 STM32 使用同一个 Client ID。

## 2. 总体控制链路

```text
PC 浏览器网页
  -> HTTP POST /api/actuators/<id>/set
  -> RK3568 iotgw_gateway
  -> Mosquitto Broker: 127.0.0.1:1883
  -> MQTT topic: iotgw/dev/command/<id>
  -> STM32 subscribe iotgw/dev/command/#
  -> STM32 解析 JSON
  -> STM32 控制 GPIO/PWM/电机驱动/蜂鸣器
```

其中 `<id>` 只有三个：

```text
led
motor
buzzer
```

## 3. Topic 约定

### 3.1 STM32 必须订阅的控制 topic

STM32 端必须订阅：

```text
iotgw/dev/command/#
```

或者分别订阅：

```text
iotgw/dev/command/led
iotgw/dev/command/motor
iotgw/dev/command/buzzer
```

推荐订阅通配符：

```text
iotgw/dev/command/#
```

收到后再根据 topic 最后一段或 JSON 里的 `device_id` 分发。

### 3.2 不要把 telemetry 当控制命令

下面这些是 STM32 上传传感器数据给 RK3568 用的 topic：

```text
iotgw/dev/telemetry/temp
iotgw/dev/telemetry/humi
iotgw/dev/telemetry/light
iotgw/dev/telemetry/ir
```

它们不是网页控制外设的 topic。

外设控制统一使用：

```text
iotgw/dev/command/...
```

## 4. RK3568 上确认命令已经发出

在 RK3568 上开一个 SSH 终端，执行：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/command/#' -v
```

然后在网页上操作 LED、电机、蜂鸣器。

如果终端能看到下面类似输出，说明：

```text
网页 -> RK3568 网关 -> Mosquitto Broker
```

这一段已经正常。

示例输出：

```text
iotgw/dev/command/led {"device_id":"led","type":"cmd","data":{"on":1,"br":96}}
iotgw/dev/command/motor {"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
iotgw/dev/command/buzzer {"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

如果这里能看到，但 STM32 外设不动作，排查重点在：

```text
STM32 是否收到 MQTT 消息
STM32 是否解析嵌套 JSON 正确
STM32 GPIO/PWM/驱动电路是否执行正确
```

## 5. JSON 总格式

三个外设命令都使用同一层结构：

```json
{
  "device_id": "设备ID",
  "type": "cmd",
  "data": {
    "字段": "值"
  }
}
```

重要说明：

```text
on、br、sp、dir 都在 data 里面。
不是根节点字段。
```

错误解析方式：

```c
root["on"]
root["br"]
root["sp"]
root["dir"]
```

正确解析方式：

```c
root["data"]["on"]
root["data"]["br"]
root["data"]["sp"]
root["data"]["dir"]
```

## 6. LED 照明灯命令

### 6.1 硬件对应

```text
网页 LED 照明灯 -> STM32 A2 / PA2
```

注意：

```text
A0 / PA0 是 STM32 本地状态灯，不是网页控制的 LED。
网页控制的是 A2 / PA2。
```

### 6.2 网页 HTTP 请求

网页操作 LED 时，请求 RK3568 网关：

```text
POST /api/actuators/led/set
Content-Type: application/json
```

网页发给网关的 body 示例：

```json
{"on":1,"br":96}
```

### 6.3 网关发布到 MQTT 的 topic

```text
iotgw/dev/command/led
```

### 6.4 网关发布到 MQTT 的 payload

打开 LED，亮度 96%：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":96}}
```

关闭 LED，亮度值仍会带上当前滑块值：

```json
{"device_id":"led","type":"cmd","data":{"on":0,"br":96}}
```

亮度调到 20%，LED 打开：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":20}}
```

亮度调到 80%，LED 打开：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":80}}
```

### 6.5 字段含义

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `led` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number | `0` 关闭 LED，`1` 打开 LED |
| `data.br` | number | LED 亮度百分比，范围 `0` 到 `100` |

### 6.6 STM32 执行要求

STM32 收到 LED 命令后，应执行：

```text
if data.on == 0:
    关闭 PA2 输出
    LED 熄灭

if data.on == 1:
    根据 data.br 设置 PA2 亮度
```

如果 A2 / PA2 只是普通 GPIO，则只能实现开关，不能实现亮度调节。

如果要实现亮度调节，A2 / PA2 必须满足其中一种：

```text
方式 1: PA2 配置为 PWM 输出，data.br 映射到 PWM 占空比
方式 2: PA2 接外部支持调光的驱动，data.br 映射到驱动输入
```

推荐 PWM 映射：

```text
duty = data.br / 100.0 * PWM_MAX
```

示例：

```text
br = 0   -> duty = 0%
br = 20  -> duty = 20%
br = 50  -> duty = 50%
br = 80  -> duty = 80%
br = 100 -> duty = 100%
```

如果 LED 是低电平点亮，则需要反相：

```text
实际 duty = 100% - br
```

这一点要以 STM32 板子实际 LED 电路为准。

### 6.7 LED 手动测试命令

在 RK3568 上执行：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH

mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/led -m '{"device_id":"led","type":"cmd","data":{"on":1,"br":20}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/led -m '{"device_id":"led","type":"cmd","data":{"on":1,"br":80}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/led -m '{"device_id":"led","type":"cmd","data":{"on":0,"br":80}}'
```

预期：

```text
第一条: LED 低亮度亮起
第二条: LED 明显变亮
第三条: LED 熄灭
```

如果第一条和第二条亮度没有区别，但开关有效，大概率是 PA2 没有做 PWM 亮度控制，或者 `data.br` 没有参与占空比计算。

## 7. 直流电机命令

### 7.1 硬件对应

按当前硬件说明，建议对应关系为：

```text
PA3 / TIM2_CH4: 电机 PWM 调速
PB14: 电机方向 AIN1
PB15: 电机方向 AIN2
```

如果使用 TB6612FNG、L298N 或其他电机驱动模块，还要确认：

```text
驱动电机电源 VM 是否供电
驱动逻辑电源 VCC 是否供电
STM32 GND 和驱动 GND 是否共地
驱动 STBY / EN 引脚是否已经拉到使能状态
电机接线是否正确
```

### 7.2 网页 HTTP 请求

网页操作电机时，请求 RK3568 网关：

```text
POST /api/actuators/motor/set
Content-Type: application/json
```

网页发给网关的 body 示例：

```json
{"on":1,"sp":53,"dir":1}
```

### 7.3 网关发布到 MQTT 的 topic

```text
iotgw/dev/command/motor
```

### 7.4 网关发布到 MQTT 的 payload

开启电机，速度 53%，反向：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
```

关闭电机，速度和方向仍会带上当前网页状态：

```json
{"device_id":"motor","type":"cmd","data":{"on":0,"sp":53,"dir":1}}
```

开启电机，速度 50%，正向：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":50,"dir":0}}
```

开启电机，速度 50%，反向：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":50,"dir":1}}
```

### 7.5 字段含义

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `motor` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number | `0` 关闭电机，`1` 开启电机 |
| `data.sp` | number | 电机速度百分比，范围 `0` 到 `100` |
| `data.dir` | number | 电机方向，`0` 正向，`1` 反向 |

### 7.6 STM32 执行要求

STM32 收到电机命令后，应执行：

```text
if data.on == 0:
    PWM duty = 0
    电机停止
    可选: AIN1 = 0, AIN2 = 0

if data.on == 1:
    根据 data.sp 设置 PA3 / TIM2_CH4 PWM 占空比
    根据 data.dir 设置方向引脚
```

推荐方向逻辑：

```text
dir = 0 正向:
    PB14 / AIN1 = 1
    PB15 / AIN2 = 0

dir = 1 反向:
    PB14 / AIN1 = 0
    PB15 / AIN2 = 1
```

推荐速度映射：

```text
duty = data.sp / 100.0 * PWM_MAX
```

示例：

```text
sp = 0   -> duty = 0%
sp = 30  -> duty = 30%
sp = 50  -> duty = 50%
sp = 80  -> duty = 80%
sp = 100 -> duty = 100%
```

如果电机有最低启动占空比，比如低于 25% 不转，可以在 STM32 端做启动保护：

```text
if data.on == 1 and data.sp > 0 and data.sp < 25:
    duty = 25%
else:
    duty = data.sp
```

但这个保护需要双方确认后再做，避免网页 10% 速度却实际 25% 速度造成误解。

### 7.7 网页交互说明

网页点击正向或反向按钮时，会自动把电机开关置为开启，并发送完整 motor 命令。

也就是说，点击方向按钮后，STM32 应收到类似：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":0}}
```

或：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
```

### 7.8 电机手动测试命令

在 RK3568 上执行：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH

mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/motor -m '{"device_id":"motor","type":"cmd","data":{"on":1,"sp":30,"dir":0}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/motor -m '{"device_id":"motor","type":"cmd","data":{"on":1,"sp":80,"dir":0}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/motor -m '{"device_id":"motor","type":"cmd","data":{"on":1,"sp":50,"dir":1}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/motor -m '{"device_id":"motor","type":"cmd","data":{"on":0,"sp":50,"dir":1}}'
```

预期：

```text
第一条: 电机正向低速转
第二条: 电机正向明显加速
第三条: 电机反向转
第四条: 电机停止
```

如果 STM32 串口能打印收到 motor 命令，但电机完全没反应，优先检查：

```text
PA3 PWM 是否启动
PWM 定时器通道是否正确
电机驱动 EN/STBY 是否使能
电机外部供电是否存在
STM32 和电机驱动是否共地
PB14/PB15 是否配置为推挽输出
```

## 8. 蜂鸣器命令

### 8.1 硬件对应

按当前硬件说明：

```text
PB3: 蜂鸣器控制
```

### 8.2 网页 HTTP 请求

网页操作蜂鸣器时，请求 RK3568 网关：

```text
POST /api/actuators/buzzer/set
Content-Type: application/json
```

网页发给网关的 body 示例：

```json
{"on":1}
```

或：

```json
{"on":0}
```

### 8.3 网关发布到 MQTT 的 topic

```text
iotgw/dev/command/buzzer
```

### 8.4 网关发布到 MQTT 的 payload

开启蜂鸣器：

```json
{"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

关闭蜂鸣器：

```json
{"device_id":"buzzer","type":"cmd","data":{"on":0}}
```

### 8.5 字段含义

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `buzzer` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number | `0` 关闭蜂鸣器，`1` 开启蜂鸣器 |

### 8.6 STM32 执行要求

STM32 收到蜂鸣器命令后，应执行：

```text
if data.on == 0:
    关闭蜂鸣器

if data.on == 1:
    开启蜂鸣器
```

注意区分蜂鸣器类型：

```text
有源蜂鸣器:
    通常 GPIO 输出高电平或低电平即可响。

无源蜂鸣器:
    需要 PWM 方波，例如 2kHz 左右，单纯 GPIO 输出固定高电平可能不会响。
```

还要确认蜂鸣器电平是否反相：

```text
高电平响: on=1 -> PB3=1, on=0 -> PB3=0
低电平响: on=1 -> PB3=0, on=0 -> PB3=1
```

蜂鸣器不是“收到 on=1 就响一下然后自动关”，而是保持状态：

```text
收到 on=1 -> 保持开启
收到 on=0 -> 保持关闭
```

### 8.7 蜂鸣器手动测试命令

在 RK3568 上执行：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH

mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/buzzer -m '{"device_id":"buzzer","type":"cmd","data":{"on":1}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/buzzer -m '{"device_id":"buzzer","type":"cmd","data":{"on":0}}'
```

预期：

```text
第一条: 蜂鸣器响
第二条: 蜂鸣器停止
```

如果 STM32 串口能打印收到 buzzer 命令，但不响，优先检查：

```text
PB3 是否被其他功能占用，比如 JTAG/SWO 或复用功能
PB3 是否配置为 GPIO 输出或 PWM 输出
蜂鸣器是有源还是无源
蜂鸣器是否需要反相电平
蜂鸣器供电是否正确
```

## 9. STM32 必须增加的串口日志

为了快速定位问题，STM32 端收到每条 MQTT 消息后，请先打印原始 topic 和原始 payload。

建议格式：

```text
MQTT RX topic=iotgw/dev/command/led payload={"device_id":"led","type":"cmd","data":{"on":1,"br":80}}
```

解析完成后，再打印解析结果。

LED 示例：

```text
CMD LED on=1 br=80
```

Motor 示例：

```text
CMD MOTOR on=1 sp=53 dir=1
```

Buzzer 示例：

```text
CMD BUZZER on=1
```

硬件执行后，再打印执行结果。

LED 示例：

```text
LED APPLY PA2 pwm_duty=80%
```

Motor 示例：

```text
MOTOR APPLY PA3 duty=53% PB14=0 PB15=1
```

Buzzer 示例：

```text
BUZZER APPLY PB3=1
```

如果加上这些日志，就能分清楚问题在三层中的哪一层：

```text
没有 MQTT RX:
    MQTT 连接或订阅问题。

有 MQTT RX，但没有 CMD 解析日志:
    JSON 解析问题。

有 CMD 解析日志，但硬件不动作:
    GPIO/PWM/驱动电路问题。
```

## 10. 推荐 STM32 解析伪代码

下面伪代码只表达字段层级，不限制具体 JSON 库。

```c
void on_mqtt_message(const char *topic, const char *payload) {
    printf("MQTT RX topic=%s payload=%s\r\n", topic, payload);

    Json root = json_parse(payload);
    const char *device_id = root["device_id"];
    const char *type = root["type"];
    Json data = root["data"];

    if (strcmp(type, "cmd") != 0) {
        printf("MQTT ignored: type is not cmd\r\n");
        return;
    }

    if (strcmp(topic, "iotgw/dev/command/led") == 0 ||
        strcmp(device_id, "led") == 0) {
        int on = data["on"];
        int br = data["br"];
        printf("CMD LED on=%d br=%d\r\n", on, br);
        led_set(on, br);
        return;
    }

    if (strcmp(topic, "iotgw/dev/command/motor") == 0 ||
        strcmp(device_id, "motor") == 0) {
        int on = data["on"];
        int sp = data["sp"];
        int dir = data["dir"];
        printf("CMD MOTOR on=%d sp=%d dir=%d\r\n", on, sp, dir);
        motor_set(on, sp, dir);
        return;
    }

    if (strcmp(topic, "iotgw/dev/command/buzzer") == 0 ||
        strcmp(device_id, "buzzer") == 0) {
        int on = data["on"];
        printf("CMD BUZZER on=%d\r\n", on);
        buzzer_set(on);
        return;
    }

    printf("MQTT ignored: unknown device_id=%s\r\n", device_id);
}
```

关键点：

```text
必须读取 root["data"]["on"]，不是 root["on"]。
必须读取 root["data"]["br"]，不是 root["br"]。
必须读取 root["data"]["sp"]，不是 root["sp"]。
必须读取 root["data"]["dir"]，不是 root["dir"]。
```

## 11. 一步一步验收流程

### 11.1 确认 Broker 正常

RK3568 上执行：

```bash
netstat -lntp 2>/dev/null | grep 1883
```

预期能看到 `0.0.0.0:1883` 或 `127.0.0.1:1883` 监听。

如果 STM32 要从网络连接，Broker 必须允许局域网访问，建议看到：

```text
0.0.0.0:1883
```

### 11.2 确认网页命令到了 Broker

RK3568 上执行：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/command/#' -v
```

网页操作外设。

预期能看到：

```text
iotgw/dev/command/led ...
iotgw/dev/command/motor ...
iotgw/dev/command/buzzer ...
```

### 11.3 确认 STM32 收到 MQTT

STM32 串口必须打印：

```text
MQTT RX topic=... payload=...
```

如果 RK3568 的 `mosquitto_sub` 能看到，但 STM32 串口没有 `MQTT RX`，检查：

```text
STM32 是否连接 192.168.31.238:1883
STM32 是否订阅 iotgw/dev/command/#
STM32 MQTT 是否断线后没有重连
STM32 重连后是否忘记重新订阅
Client ID 是否和其他客户端重复
Wi-Fi/以太网是否稳定
```

### 11.4 确认 STM32 解析正确

STM32 串口必须打印：

```text
CMD LED on=... br=...
CMD MOTOR on=... sp=... dir=...
CMD BUZZER on=...
```

如果有 `MQTT RX`，但没有 `CMD ...`，检查：

```text
JSON 库是否成功解析 payload
是否把 on/br/sp/dir 当成根节点字段读取
是否漏了 data 这一层
是否把数字 1/0 当成字符串处理失败
topic 字符串比较是否完全一致
```

### 11.5 确认硬件执行

STM32 串口必须打印：

```text
LED APPLY ...
MOTOR APPLY ...
BUZZER APPLY ...
```

如果解析正确但硬件不动作，检查硬件层：

```text
LED: PA2 是否 PWM，是否低电平点亮，br 是否映射到 duty
Motor: PA3 PWM 是否启动，PB14/PB15 是否输出，驱动 EN/STBY/电源/共地是否正常
Buzzer: PB3 是否配置正确，有源/无源蜂鸣器是否匹配，高低电平是否反相
```

## 12. 常见错误

### 12.1 只解析根节点，没解析 data

错误：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
```

却用：

```c
on = root["on"];
sp = root["sp"];
dir = root["dir"];
```

这样会解析不到。

正确：

```c
on = root["data"]["on"];
sp = root["data"]["sp"];
dir = root["data"]["dir"];
```

### 12.2 LED 开关有效但亮度无效

如果 LED 能开关，但 `br=20` 和 `br=80` 亮度差不多，通常是：

```text
PA2 只当普通 GPIO 使用
data.br 没有映射到 PWM duty
PWM 定时器没有启动
LED 电路需要反相，但没有反相
```

### 12.3 电机完全没反应

如果 motor 命令能收到但电机不动，通常是：

```text
PA3 PWM 没启动
电机驱动没有使能
电机驱动没有外部电源
STM32 和驱动没有共地
PB14/PB15 方向脚没输出
sp 太低，电机启动不了
```

### 12.4 蜂鸣器收到命令但不响

如果 buzzer 命令能收到但不响，通常是：

```text
蜂鸣器是无源蜂鸣器，需要 PWM 方波
PB3 被复用功能占用
PB3 电平需要反相
蜂鸣器供电或接线问题
```

### 12.5 MQTT 连接不稳定

如果 STM32 MQTT 经常断线，建议：

```text
断线后自动重连
重连成功后必须重新 subscribe iotgw/dev/command/#
不要多个设备使用同一个 Client ID
保持 keepalive，例如 30 秒
不要高频 publish 控制命令
确认 Wi-Fi RSSI 或网线连接稳定
```

## 13. 最终双方约定

STM32 上传传感器数据：

```text
publish -> iotgw/dev/telemetry/temp
publish -> iotgw/dev/telemetry/humi
publish -> iotgw/dev/telemetry/light
publish -> iotgw/dev/telemetry/ir
payload -> {"value":数字}
```

STM32 接收网页控制命令：

```text
subscribe -> iotgw/dev/command/#
```

LED 控制：

```text
topic: iotgw/dev/command/led
payload: {"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
```

电机控制：

```text
topic: iotgw/dev/command/motor
payload: {"device_id":"motor","type":"cmd","data":{"on":1,"sp":50,"dir":0}}
```

蜂鸣器控制：

```text
topic: iotgw/dev/command/buzzer
payload: {"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

最重要的一句话：

```text
STM32 接收控制命令时，必须订阅 iotgw/dev/command/#，并解析 data 里面的 on/br/sp/dir。
```
