# STM32 外设控制 MQTT 命令排查说明

本文档专门用于排查网页右侧“硬件外设控制”区域：

- LED 照明灯
- 直流电机控制
- 紧急蜂鸣报警

当前现象是：RK3568 上用 `mosquitto_sub` 已经能监听到网关发布的 JSON 命令，但 STM32 外设动作不符合预期。本文档精确说明网页/网关发了什么、发到哪里、字段含义是什么，以及 STM32 端应该如何解析和执行。

## 1. 当前控制链路

```text
PC 网页
  -> HTTP POST /api/actuators/<id>/set
  -> RK3568 iotgw_gateway
  -> Mosquitto Broker: 127.0.0.1:1883
  -> MQTT topic: iotgw/dev/command/<id>
  -> STM32 subscribe iotgw/dev/command/#
  -> STM32 解析 JSON 并控制 GPIO/PWM
```

其中：

```text
<id> = led / motor / buzzer
```

STM32 只需要订阅：

```text
iotgw/dev/command/#
```

不要订阅 `iotgw/dev/telemetry/#` 来接收控制命令。`telemetry` 是单片机上传传感器数据用的，`command` 才是网关控制单片机用的。

## 2. RK3568 上如何确认命令已经发出

在 RK3568 板子上开一个 SSH 窗口：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/command/#' -v
```

然后在网页上操作 LED、电机、蜂鸣器。窗口里应该能看到类似：

```text
iotgw/dev/command/led {"device_id":"led","type":"cmd","data":{"on":1,"br":96}}
iotgw/dev/command/motor {"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
iotgw/dev/command/buzzer {"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

如果这里能看到，说明：

```text
网页 -> 网关 -> MQTT Broker
```

这半段是通的。后续重点检查 STM32 是否收到、是否解析正确、GPIO/PWM 是否执行正确。

## 3. LED 照明灯命令

### 3.1 硬件对应

```text
网页 LED 照明灯 -> STM32 A2 / PA2
```

注意：

```text
A0 / PA0 是 STM32 本地状态灯，不受网页控制。
```

### 3.2 MQTT Topic

```text
iotgw/dev/command/led
```

### 3.3 网页开 LED 时发出的 JSON

例如亮度为 96%，开关为开：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":96}}
```

### 3.4 网页关 LED 时发出的 JSON

例如亮度仍为 96%，开关为关：

```json
{"device_id":"led","type":"cmd","data":{"on":0,"br":96}}
```

### 3.5 字段含义

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `led` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number | `0` 关闭 LED，`1` 打开 LED |
| `data.br` | number | LED 亮度百分比，范围 `0` 到 `100` |

### 3.6 STM32 应执行逻辑

```text
if data.on == 0:
    PA2 输出关闭
    LED 熄灭

if data.on == 1:
    根据 data.br 设置 PA2 亮度
```

如果 PA2 是普通 GPIO，不支持 PWM，那么 `br` 不会产生亮度变化，只能开/关。这种情况下网页亮度条怎么调，硬件亮度都不会变。

如果需要亮度可调，PA2 必须使用可调 PWM 或者外接支持调光的驱动方式。STM32 端要确认：

```text
PA2 是否配置为 PWM 输出
data.br 是否映射到 PWM 占空比
```

推荐映射：

```text
duty = data.br / 100.0 * PWM_MAX
```

例如：

```text
br = 0   -> duty = 0
br = 50  -> duty = 50%
br = 100 -> duty = 100%
```

### 3.7 LED 手动测试命令

在 RK3568 上手动发，绕过网页：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/led -m '{"device_id":"led","type":"cmd","data":{"on":1,"br":20}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/led -m '{"device_id":"led","type":"cmd","data":{"on":1,"br":80}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/led -m '{"device_id":"led","type":"cmd","data":{"on":0,"br":80}}'
```

期望：

```text
第一条: LED 较暗
第二条: LED 较亮
第三条: LED 熄灭
```

如果第一条和第二条亮度一样，只能说明 STM32 的 PA2 当前没有按 `br` 做 PWM 调光，或者硬件不支持调光。

## 4. 直流电机命令

### 4.1 硬件对应

```text
PWM 调速: PA3 / TIM2_CH4
方向 AIN1: PB14
方向 AIN2: PB15
```

### 4.2 MQTT Topic

```text
iotgw/dev/command/motor
```

### 4.3 网页打开电机时发出的 JSON

例如速度 53%，方向为反转：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
```

### 4.4 网页关闭电机时发出的 JSON

```json
{"device_id":"motor","type":"cmd","data":{"on":0,"sp":53,"dir":1}}
```

### 4.5 网页点击正向旋转时发出的 JSON

网页点击正向旋转会自动把电机开关置为开，并发送：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":0}}
```

### 4.6 网页点击反向旋转时发出的 JSON

网页点击反向旋转会自动把电机开关置为开，并发送：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
```

### 4.7 字段含义

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `motor` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number | `0` 停止，`1` 运行 |
| `data.sp` | number | 电机速度百分比，范围 `0` 到 `100` |
| `data.dir` | number | `0` 正转，`1` 反转 |

### 4.8 STM32 应执行逻辑

```text
if data.on == 0:
    PA3 PWM = 0
    PB14 = 0
    PB15 = 0
    电机停止

if data.on == 1:
    if data.dir == 0:
        PB14 = 1
        PB15 = 0
    if data.dir == 1:
        PB14 = 0
        PB15 = 1

    PA3 PWM duty = data.sp / 100.0 * PWM_MAX
```

如果 `data.on == 1` 但电机不动，优先检查：

```text
1. STM32 是否真的解析到 data.on = 1
2. PA3 是否已经启动 PWM 输出
3. TIM2_CH4 是否配置正确
4. TB6612FNG 的 STBY / VM / VCC / GND 是否正确
5. PB14 / PB15 是否按 dir 正确输出
6. data.sp 是否大于 0
7. 电机电源是否独立且供电足够
```

如果方向变化没有效果，重点检查：

```text
PB14 / PB15 是否接反
dir=0 时 PB14=1 PB15=0
dir=1 时 PB14=0 PB15=1
```

### 4.9 电机手动测试命令

在 RK3568 上手动发，绕过网页：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH

mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/motor -m '{"device_id":"motor","type":"cmd","data":{"on":1,"sp":30,"dir":0}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/motor -m '{"device_id":"motor","type":"cmd","data":{"on":1,"sp":80,"dir":0}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/motor -m '{"device_id":"motor","type":"cmd","data":{"on":1,"sp":50,"dir":1}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/motor -m '{"device_id":"motor","type":"cmd","data":{"on":0,"sp":50,"dir":1}}'
```

期望：

```text
第一条: 正转，低速
第二条: 正转，高速
第三条: 反转，中速
第四条: 停止
```

如果这些命令在 `mosquitto_sub` 能看到，但 STM32 没反应，问题在 STM32 订阅、JSON 解析、PWM/GPIO 或电机驱动硬件侧。

## 5. 蜂鸣器命令

### 5.1 硬件对应

```text
蜂鸣器: PB3
```

### 5.2 MQTT Topic

```text
iotgw/dev/command/buzzer
```

### 5.3 网页打开蜂鸣器时发出的 JSON

```json
{"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

### 5.4 网页关闭蜂鸣器时发出的 JSON

```json
{"device_id":"buzzer","type":"cmd","data":{"on":0}}
```

### 5.5 字段含义

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `device_id` | string | 固定为 `buzzer` |
| `type` | string | 固定为 `cmd` |
| `data.on` | number | `0` 关闭，`1` 开启 |

### 5.6 STM32 应执行逻辑

一般约定：

```text
data.on == 1 -> PB3 输出高电平，蜂鸣器响
data.on == 0 -> PB3 输出低电平，蜂鸣器不响
```

但蜂鸣器模块可能是低电平触发。如果网页打开蜂鸣器后 PB3 已经变高，但蜂鸣器不响，需要确认模块触发极性：

```text
高电平触发: on=1 -> PB3=1
低电平触发: on=1 -> PB3=0
```

如果是无源蜂鸣器，仅输出高电平可能不会持续发声，需要 PWM 方波驱动。STM32 端需要确认蜂鸣器类型：

```text
有源蜂鸣器: GPIO 高/低电平即可响
无源蜂鸣器: 需要 PWM 方波
```

### 5.7 蜂鸣器手动测试命令

在 RK3568 上手动发，绕过网页：

```bash
export LD_LIBRARY_PATH=/usr/lib:$LD_LIBRARY_PATH

mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/buzzer -m '{"device_id":"buzzer","type":"cmd","data":{"on":1}}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/command/buzzer -m '{"device_id":"buzzer","type":"cmd","data":{"on":0}}'
```

期望：

```text
第一条: 蜂鸣器响
第二条: 蜂鸣器停止
```

如果 MQTT 收到了但不响，优先检查：

```text
1. STM32 是否解析到 data.on = 1
2. PB3 是否配置为输出
3. PB3 电平是否真的变化
4. 蜂鸣器是高电平触发还是低电平触发
5. 蜂鸣器是有源还是无源
6. 蜂鸣器供电和 GND 是否正确
```

## 6. STM32 串口必须打印的调试信息

为了判断问题在 JSON 解析还是硬件控制，STM32 收到命令后建议打印：

### 6.1 收到原始 MQTT

```text
MQTT CMD topic=iotgw/dev/command/led payload={"device_id":"led","type":"cmd","data":{"on":1,"br":96}}
MQTT CMD topic=iotgw/dev/command/motor payload={"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
MQTT CMD topic=iotgw/dev/command/buzzer payload={"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

### 6.2 JSON 解析结果

LED：

```text
LED parsed: on=1 br=96
```

电机：

```text
MOTOR parsed: on=1 sp=53 dir=1
```

蜂鸣器：

```text
BUZZER parsed: on=1
```

### 6.3 实际引脚输出

LED：

```text
LED apply: PA2 pwm_duty=96%
```

电机：

```text
MOTOR apply: PA3 pwm_duty=53%, PB14=0, PB15=1
```

蜂鸣器：

```text
BUZZER apply: PB3=1
```

如果串口能打印原始 MQTT，但解析结果不对，问题在 JSON 解析。

如果解析结果对，但引脚输出不对，问题在 STM32 控制逻辑。

如果引脚输出对，但硬件不动作，问题在硬件接线、驱动、电源、触发极性或 PWM 配置。

## 7. STM32 JSON 解析注意点

payload 是嵌套 JSON：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
```

字段 `on/sp/dir/br` 在 `data` 对象里面，不在最外层。

错误解析方式：

```text
直接从根对象读取 on
直接从根对象读取 sp
直接从根对象读取 dir
```

正确解析方式：

```text
先获取 data 对象
再从 data 对象读取 on/br/sp/dir
```

伪代码：

```c
root = json_parse(payload);
device_id = root["device_id"];
data = root["data"];

if device_id == "led":
    on = data["on"];
    br = data["br"];

if device_id == "motor":
    on = data["on"];
    sp = data["sp"];
    dir = data["dir"];

if device_id == "buzzer":
    on = data["on"];
```

如果 STM32 JSON 库不支持嵌套对象路径，不能写成：

```text
json_get_int(payload, "data.on")
```

必须按该 JSON 库的方式先解析 `data` 子对象。

## 8. 当前网页实际发送规则

### 8.1 LED

用户操作：

```text
点击 LED 开关
拖动 LED 亮度滑条
```

网页请求：

```text
POST /api/actuators/led/set
```

HTTP body 示例：

```json
{"on":1,"br":96}
```

网关发布 MQTT：

```text
topic: iotgw/dev/command/led
payload: {"device_id":"led","type":"cmd","data":{"on":1,"br":96}}
```

### 8.2 电机

用户操作：

```text
点击电机开关
拖动电机速度滑条
点击正向旋转
点击反向旋转
```

网页请求：

```text
POST /api/actuators/motor/set
```

HTTP body 示例：

```json
{"on":1,"sp":53,"dir":1}
```

网关发布 MQTT：

```text
topic: iotgw/dev/command/motor
payload: {"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
```

说明：

```text
点击正向/反向按钮时，网页会自动把 motor on 设置为 1。
```

### 8.3 蜂鸣器

用户操作：

```text
点击紧急蜂鸣报警开关
```

网页请求：

```text
POST /api/actuators/buzzer/set
```

HTTP body 示例：

```json
{"on":1}
```

网关发布 MQTT：

```text
topic: iotgw/dev/command/buzzer
payload: {"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

## 9. 判断责任边界

### 9.1 网关侧正常的证据

在 RK3568 上监听：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/command/#' -v
```

如果能看到正确 topic 和 JSON，例如：

```text
iotgw/dev/command/motor {"device_id":"motor","type":"cmd","data":{"on":1,"sp":53,"dir":1}}
```

说明网关已经成功发出控制命令。

### 9.2 STM32 侧需要继续检查的证据

STM32 串口应该打印：

```text
收到 topic
收到 payload
解析出来的 on/br/sp/dir
实际设置的 GPIO/PWM
```

如果 STM32 串口没有收到 topic，检查：

```text
是否订阅 iotgw/dev/command/#
MQTT 重连后是否重新订阅
Client ID 是否冲突
网络是否断线
```

如果 STM32 收到 topic 但解析失败，检查：

```text
是否正确解析嵌套 data 对象
数字字段是否按 number 解析
是否把 payload 当成普通字符串处理但没有 JSON 解析
```

如果解析成功但硬件不动作，检查：

```text
GPIO 模式
PWM 初始化
定时器通道
驱动芯片使能脚
电机/蜂鸣器供电
触发电平极性
GND 共地
```

## 10. 最小联调顺序

建议不要一开始就同时测网页、网关、STM32、硬件。

按下面顺序：

1. RK3568 手动 `mosquitto_pub` 发 LED 命令，看 STM32 串口是否收到。
2. STM32 收到后只打印解析结果，不控制硬件。
3. 解析正确后，再控制 PA2 LED。
4. LED 成功后，再测蜂鸣器 PB3。
5. 蜂鸣器成功后，再测电机方向 PB14/PB15。
6. 方向成功后，再测 PA3 PWM 调速。
7. 最后再回到网页按钮测试。

这样能明确问题到底在 MQTT、JSON、GPIO、PWM 还是硬件供电。

