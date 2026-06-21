# ZigBee DL-20 透明串口对接说明

这份文档给 STM32/单片机端开发使用。当前 RK3568 网关已经支持 ZigBee DL-20 透明串口通信，网页上选择 `ZigBee` 后，LED、电机、蜂鸣器控制命令会通过 RK3568 的串口发给单片机；单片机也可以通过 ZigBee 串口把传感器数据发回 RK3568。

当前网关 ZigBee 状态接口：

```text
GET /api/zigbee/status
```

已确认返回：

```json
{
  "ok": true,
  "open": true,
  "device": "/dev/ttyS4",
  "baudrate": 9600
}
```

这表示 RK3568 端已经打开 `/dev/ttyS4`，可以开始和单片机联调。

## 1. 串口参数

RK3568 与 DL-20 使用透明串口传输：

```text
设备: /dev/ttyS4
波特率: 9600
数据位: 8
停止位: 1
校验: 无
流控: 无
电平: 3.3V TTL
```

RK3568 侧接线：

```text
RK3568 TX -> DL-20 RX
RK3568 RX -> DL-20 TX
RK3568 GND -> DL-20 GND
```

STM32 侧接线：

```text
STM32 TX -> DL-20 RX
STM32 RX -> DL-20 TX
STM32 GND -> DL-20 GND
```

注意：TX/RX 要交叉，GND 必须共地。

## 2. 数据格式总原则

ZigBee 这里是透明串口，不是 MQTT。

所以 ZigBee 通道没有 topic，只有串口内容。

所有数据统一要求：

```text
一行一个 JSON
每条 JSON 最后必须带 \n
```

正确示例：

```text
{"type":"telemetry","id":"temp","value":25.6}\n
```

错误示例：

```text
{"type":"telemetry","id":"temp","value":25.6}
```

上面这个错误示例缺少结尾换行符，网关可能会一直等待一整行，导致网页不更新。

不要这样发：

```text
半条 JSON
多条 JSON 粘在同一行
JSON 外面再包一层转义字符串
```

单片机实际发送内容必须是：

```json
{"type":"telemetry","id":"temp","value":25.6}
```

不要发成：

```text
{\"type\":\"telemetry\",\"id\":\"temp\",\"value\":25.6}
```

## 3. 单片机发给 RK3568：传感器数据

单片机通过 ZigBee 发传感器数据时，格式固定为：

```json
{"type":"telemetry","id":"传感器ID","value":数字}
```

每条末尾都要带 `\n`。

温度：

```json
{"type":"telemetry","id":"temp","value":25.6}
```

湿度：

```json
{"type":"telemetry","id":"humi","value":60.2}
```

光照：

```json
{"type":"telemetry","id":"light","value":820}
```

红外：

```json
{"type":"telemetry","id":"ir","value":1}
```

红外统一约定：

```text
0: 安全 / 无人 / 未触发
1: 有人 / 触发
```

推荐发送频率：

```text
温度: 1 秒一次
湿度: 1 秒一次
光照: 0.5 到 1 秒一次
红外: 状态变化时发送，或者 0.5 到 1 秒一次
```

初期联调不要高频狂发，比如不要 10ms 一轮持续发送。

STM32 伪代码示例：

```c
uart_send("{\"type\":\"telemetry\",\"id\":\"temp\",\"value\":25.6}\n");
delay_ms(30);
uart_send("{\"type\":\"telemetry\",\"id\":\"humi\",\"value\":60.2}\n");
delay_ms(30);
uart_send("{\"type\":\"telemetry\",\"id\":\"light\",\"value\":820}\n");
delay_ms(30);
uart_send("{\"type\":\"telemetry\",\"id\":\"ir\",\"value\":1}\n");
```

## 4. RK3568 发给单片机：控制命令

网页选择 `ZigBee` 后，RK3568 会通过 `/dev/ttyS4` 给单片机发送控制命令。

控制命令也是一行一个 JSON，末尾带 `\n`。

### 4.1 LED 命令

打开 LED，亮度 50：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
```

关闭 LED：

```json
{"device_id":"led","type":"cmd","data":{"on":0,"br":50}}
```

字段说明：

```text
device_id: 固定为 led
type: 固定为 cmd
data.on: 0 关闭，1 打开
data.br: 亮度百分比，0 到 100
```

STM32 执行要求：

```text
if data.on == 0:
    关闭 LED
else:
    打开 LED
    按 data.br 设置 PWM 占空比
```

如果 LED 是低电平点亮，需要 STM32 端做反相处理。

### 4.2 电机命令

启动电机，速度 30，正转：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":30,"dir":0}}
```

启动电机，速度 60，反转：

```json
{"device_id":"motor","type":"cmd","data":{"on":1,"sp":60,"dir":1}}
```

停止电机：

```json
{"device_id":"motor","type":"cmd","data":{"on":0,"sp":30,"dir":0}}
```

字段说明：

```text
device_id: 固定为 motor
type: 固定为 cmd
data.on: 0 停止，1 启动
data.sp: 速度百分比，0 到 100
data.dir: 0 正转，1 反转
```

STM32 执行要求：

```text
if data.on == 0:
    电机停止
    PWM = 0
else:
    根据 data.dir 设置方向
    根据 data.sp 设置 PWM 占空比
```

方向建议：

```text
dir = 0:
    AIN1 = 1
    AIN2 = 0

dir = 1:
    AIN1 = 0
    AIN2 = 1
```

具体方向如果和实际相反，以硬件接线为准，可以在 STM32 端交换方向逻辑。

### 4.3 蜂鸣器命令

开启蜂鸣器：

```json
{"device_id":"buzzer","type":"cmd","data":{"on":1}}
```

关闭蜂鸣器：

```json
{"device_id":"buzzer","type":"cmd","data":{"on":0}}
```

字段说明：

```text
device_id: 固定为 buzzer
type: 固定为 cmd
data.on: 0 关闭，1 开启
```

STM32 执行要求：

```text
if data.on == 0:
    关闭蜂鸣器
else:
    开启蜂鸣器
```

注意区分蜂鸣器类型：

```text
有源蜂鸣器: GPIO 输出有效电平通常就能响
无源蜂鸣器: 需要 PWM 方波，例如 2kHz 左右
```

如果蜂鸣器是低电平触发，需要 STM32 端做反相处理。

## 5. 单片机解析要求

单片机收到 RK3568 发来的控制命令后，必须解析嵌套字段。

正确读取：

```text
root["device_id"]
root["type"]
root["data"]["on"]
root["data"]["br"]
root["data"]["sp"]
root["data"]["dir"]
```

不要这样读：

```text
root["on"]
root["br"]
root["sp"]
root["dir"]
```

因为 `on/br/sp/dir` 都在 `data` 对象里面。

推荐接收流程：

```text
1. 串口接收字节
2. 按 \n 拼成一行
3. 解析这一行 JSON
4. 判断 type 是否为 cmd
5. 根据 device_id 分发到 LED / motor / buzzer
6. 读取 data 里面的字段并执行硬件动作
```

推荐打印调试日志：

```text
ZIGBEE RX {"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
CMD LED on=1 br=50
LED APPLY pwm=50%
```

电机：

```text
ZIGBEE RX {"device_id":"motor","type":"cmd","data":{"on":1,"sp":30,"dir":0}}
CMD MOTOR on=1 sp=30 dir=0
MOTOR APPLY duty=30% dir=0
```

蜂鸣器：

```text
ZIGBEE RX {"device_id":"buzzer","type":"cmd","data":{"on":1}}
CMD BUZZER on=1
BUZZER APPLY on=1
```

## 6. 和 MQTT 的区别

如果走 MQTT：

```text
传感器数据有 topic
控制命令有 topic
payload 是 JSON
```

如果走 ZigBee：

```text
没有 topic
只有串口 JSON 行
一行一个 JSON
必须以 \n 结尾
```

ZigBee 传感器上报：

```json
{"type":"telemetry","id":"temp","value":25.6}
```

MQTT 传感器上报 payload：

```json
{"value":25.6}
```

这两个格式不一样，不要混用。

ZigBee 控制命令的 JSON 内容和 MQTT 控制命令 payload 基本一致：

```json
{"device_id":"led","type":"cmd","data":{"on":1,"br":50}}
```

区别只是 ZigBee 不需要 topic，直接通过串口发送这一行 JSON。

## 7. 前端 MQTT / ZigBee 切换机制

网页上的 `MQTT / ZigBee` 按钮只影响 RK3568 网关的下发通道。

也就是说，网页选择 `MQTT` 时：

```text
网页按钮
  -> RK3568 网关
  -> MQTT publish
  -> topic: iotgw/dev/command/led / motor / buzzer
  -> 单片机 MQTT 客户端收到命令
```

网页选择 `ZigBee` 时：

```text
网页按钮
  -> RK3568 网关
  -> /dev/ttyS4 串口写出 JSON 行
  -> DL-20 透明传输
  -> 单片机 UART 收到命令
```

单片机不需要等待 RK3568 额外发送“当前模式是 MQTT”或者“当前模式是 ZigBee”的通知。

推荐单片机同时监听两路：

```text
MQTT:
    subscribe iotgw/dev/command/#
    收到 payload 后解析 JSON
    调用 dispatch_command(json)

ZigBee:
    UART 按 \n 接收一行 JSON
    解析 JSON
    调用 dispatch_command(json)
```

两路最终共用同一套执行函数：

```text
dispatch_command(json):
    if device_id == "led":
        led_set(data.on, data.br)

    if device_id == "motor":
        motor_set(data.on, data.sp, data.dir)

    if device_id == "buzzer":
        buzzer_set(data.on)
```

不要因为网页切到 `ZigBee` 就关闭 MQTT 监听，也不要因为网页切到 `MQTT` 就停止 UART 监听。前端按钮只决定本次网页控制命令从哪条通道发出，不代表单片机只能保留其中一条接收链路。

如果单片机暂时只做 ZigBee，也可以只监听 UART；如果后续要同时兼容 MQTT 和 ZigBee，就按上面的双通道方式实现。

## 8. RK3568 侧手动测试

在 RK3568 上可以临时监听串口：

```bash
stty -F /dev/ttyS4 9600 cs8 -cstopb -parenb -ixon -ixoff -crtscts
cat /dev/ttyS4
```

也可以手动往单片机方向发送测试命令：

```bash
echo '{"device_id":"led","type":"cmd","data":{"on":1,"br":50}}' > /dev/ttyS4
echo '{"device_id":"motor","type":"cmd","data":{"on":1,"sp":30,"dir":0}}' > /dev/ttyS4
echo '{"device_id":"buzzer","type":"cmd","data":{"on":1}}' > /dev/ttyS4
```

注意：网关程序运行时会占用 `/dev/ttyS4`。如果要用 `cat` 或 `echo` 手动测试串口，最好先停止网关程序，避免两个进程同时操作同一个串口。

## 9. 网页联调步骤

1. 启动 RK3568 网关。
2. 浏览器打开：

```text
http://192.168.31.238:8080/
```

3. 确认 ZigBee 状态：

```text
http://192.168.31.238:8080/api/zigbee/status
```

期望：

```json
{"ok":true,"open":true,"device":"/dev/ttyS4","baudrate":9600}
```

4. 网页上选择 `ZigBee`。
5. 单片机先每秒发送一条温度：

```json
{"type":"telemetry","id":"temp","value":26.5}
```

注意末尾必须带 `\n`。

6. 确认网页温度变化。
7. 再测试湿度、光照、红外。
8. 最后测试网页按钮控制 LED、电机、蜂鸣器。

## 10. 最终确认清单

单片机交付前确认：

- 串口参数是 `9600 8N1，无校验，无流控`。
- ZigBee 模块是 `3.3V TTL`。
- TX/RX 已交叉连接。
- GND 已共地。
- 单片机发送给 RK3568 的 telemetry JSON 末尾有 `\n`。
- 单片机能按 `\n` 接收 RK3568 下发的命令。
- 单片机解析的是 `data.on`，不是根节点 `on`。
- LED 支持 `data.on` 和 `data.br`。
- 电机支持 `data.on`、`data.sp`、`data.dir`。
- 蜂鸣器支持 `data.on`。
- 命令按“设置状态”执行，不要按“翻转状态”执行。
- 收到两次 `{"on":1}` 应该保持开启，不要开关来回切换。
- 收到 `{"on":0}` 才关闭。
