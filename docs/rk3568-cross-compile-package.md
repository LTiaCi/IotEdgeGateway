# RK3568 Linux 虚拟机交叉编译、打包、上板运行步骤

这份文档按你现在的项目状态来写：在 Linux 虚拟机里交叉编译 `iotgw_gateway`，只打包运行需要的文件，然后把 `dist/iotgw_package` 放到 RK3568 板子上启动。

项目里已经内置了 Mongoose 源码，所以 Linux 虚拟机里不需要再单独 make Mongoose。

当前版本还会链接 SQLite3，用来保存传感器数据、照片/录像记录、控制命令和运行日志。板子上有 `sqlite3` 命令还不够，交叉编译 sysroot 里也要有 `sqlite3.h` 和 `libsqlite3.so`。

## 1. 整体流程

```text
Windows / 项目源码
        |
        | 拷贝项目到 Linux 虚拟机
        v
Linux 虚拟机交叉编译 ARM64 程序
        |
        | ./scripts/package_runtime.sh
        v
dist/iotgw_package
        |
        | scp / 挂载 / U 盘拷贝
        v
RK3568 板子 /opt/iotgw_package
        |
        | ./start.sh
        v
PC 浏览器打开 http://RK3568_IP:8080/
```

上板时不需要拷贝整个源码目录，只拷贝 `dist/iotgw_package` 就可以，能省很多空间。

## 2. Linux 虚拟机准备

先进入你的 Linux 虚拟机，确认这些工具有：

```bash
cmake --version
make --version
pkg-config --version
```

如果没有，可以安装：

```bash
sudo apt update
sudo apt install -y cmake make pkg-config
```

推荐使用 Buildroot 自己生成的交叉编译器，因为它和 RK3568 rootfs、系统库更匹配。

你的 Buildroot 交叉编译器路径是：

```text
C 编译器: /home/ciyeer/source/rk356x_linux/buildroot/output/host/bin/aarch64-buildroot-linux-gnu-gcc
C++ 编译器: /home/ciyeer/source/rk356x_linux/buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++
sysroot: /home/ciyeer/source/rk356x_linux/buildroot/output/host/aarch64-buildroot-linux-gnu/sysroot
```

项目里的 Buildroot 工具链文件已经按这个路径配置好了：

```text
cmake/toolchains/rk3568-buildroot.cmake
```

你可以在 Linux 虚拟机里确认一下：

```bash
cat cmake/toolchains/rk3568-buildroot.cmake
```

里面应该类似这样：

```cmake
set(CMAKE_C_COMPILER /home/ciyeer/source/rk356x_linux/buildroot/output/host/bin/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /home/ciyeer/source/rk356x_linux/buildroot/output/host/bin/aarch64-buildroot-linux-gnu-g++)
set(CMAKE_SYSROOT /home/ciyeer/source/rk356x_linux/buildroot/output/host/aarch64-buildroot-linux-gnu/sysroot)
```

再确认 SQLite3 开发文件存在：

```bash
find /home/ciyeer/source/rk356x_linux/buildroot/output/host/aarch64-buildroot-linux-gnu/sysroot -name "sqlite3.h"
find /home/ciyeer/source/rk356x_linux/buildroot/output/host/aarch64-buildroot-linux-gnu/sysroot -name "libsqlite3.so*"
```

正常应该能看到：

```text
.../usr/include/sqlite3.h
.../usr/lib/libsqlite3.so
```

如果这里查不到，CMake 会报 SQLite3 开发文件缺失，需要先在 Buildroot/sysroot 里补 SQLite3。

## 3. 把项目源码放到 Linux 虚拟机

假设你在 Linux 虚拟机里放到：

```bash
~/IoTEdgeGateway
```

进入项目根目录：

```bash
cd ~/IoTEdgeGateway
```

确认能看到这些目录：

```bash
ls
```

应该能看到：

```text
CMakeLists.txt
src
www
config
scripts
third_party
cmake
docs
```

其中 `third_party/mongoose` 必须存在：

```bash
ls third_party/mongoose
```

应该能看到：

```text
mongoose.c
mongoose.h
```

## 4. 编译方式一：不带视频功能的普通版

如果你只是先验证网页、传感器、控制按钮，不急着用摄像头视频功能，先用这个方式。这个方式最稳，依赖最少。

```bash
cd ~/IoTEdgeGateway

rm -rf build-rk3568

cmake -S . -B build-rk3568 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3568-buildroot.cmake \
  -DIOTGW_ENABLE_GSTREAMER=OFF

cmake --build build-rk3568 -j"$(nproc)"
```

编译完成后检查程序架构：

```bash
file build-rk3568/iotgw_gateway
```

正常应该包含：

```text
ELF 64-bit ... ARM aarch64
```

注意：这个版本可以正常打开网页，但是点击“开启视频”时后端会返回 `gstreamer_disabled`，因为编译时没有打开 GStreamer。

## 5. 编译方式二：带视频功能的 GStreamer 版

如果你要使用网页里的：

```text
开启视频
关闭视频
抓拍照片
视频录制
```

就要打开：

```bash
-DIOTGW_ENABLE_GSTREAMER=ON
```

当前项目的视频版后端采用运行时调用 `gst-launch-1.0` 的方式，不直接链接 GStreamer 开发库。因此 Linux 虚拟机交叉编译时不需要 `gstreamer-1.0.pc`，也不需要找到 `gst/gst.h`。

但是 RK3568 板子运行时必须有：

```text
gst-launch-1.0
gst-inspect-1.0
v4l2src
videoconvert
mpph264enc
h264parse
mpegtsmux
hlssink
ffmpeg
```

编译命令：

```bash
cd ~/IoTEdgeGateway

rm -rf build-rk3568

cmake -S . -B build-rk3568 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3568-buildroot.cmake \
  -DIOTGW_ENABLE_GSTREAMER=ON

cmake --build build-rk3568 -j"$(nproc)"
```

检查程序架构：

```bash
file build-rk3568/iotgw_gateway
```

正常应该包含：

```text
ELF 64-bit ... ARM aarch64
```

## 6. 打包运行目录

编译成功后，不要把整个源码传到板子上，只打包运行目录：

```bash
cd ~/IoTEdgeGateway

rm -rf dist/iotgw_package

./scripts/package_runtime.sh build-rk3568 dist/iotgw_package
```

成功后会生成：

```text
dist/iotgw_package
```

里面大概是：

```text
dist/iotgw_package/
├── bin/
│   └── iotgw_gateway
├── config/
├── data/
├── init.d/
│   ├── S45wifi_jk
│   ├── S70mosquitto
│   └── S85iotgw
├── www/
│   ├── index.html
│   └── js/hls.min.js
└── start.sh
```

数据库文件不会在打包时生成，而是在板子第一次启动后自动生成：

```text
/opt/iotgw_package/data/iotgw.db
```

确认一下：

```bash
ls -lh dist/iotgw_package/bin/iotgw_gateway
ls -lh dist/iotgw_package/www/index.html
ls -lh dist/iotgw_package/www/js/hls.min.js
```

## 7. 把包传到 RK3568 板子

假设 RK3568 板子的 IP 是：

```text
192.168.31.238
```

你按实际 IP 替换。

### 方式 A：scp 传输

```bash
cd ~/IoTEdgeGateway

scp -r dist/iotgw_package root@192.168.31.238:/opt/
```

如果要安装 MQTT 和网关开机自启脚本，再执行：

```bash
scp dist/iotgw_package/init.d/S45wifi_jk root@192.168.31.238:/etc/init.d/
scp dist/iotgw_package/init.d/S70mosquitto root@192.168.31.238:/etc/init.d/
scp dist/iotgw_package/init.d/S85iotgw root@192.168.31.238:/etc/init.d/

ssh root@192.168.31.238
chmod +x /etc/init.d/S45wifi_jk /etc/init.d/S70mosquitto /etc/init.d/S85iotgw
chmod +x /opt/iotgw_package/start.sh /opt/iotgw_package/bin/iotgw_gateway
```

推荐开机顺序：

```text
S45wifi_jk -> S70mosquitto -> S85iotgw -> S95qtRk
```

也就是先连接 WiFi，再启动 MQTT Broker，再启动网关后端，最后启动 Qt 界面。

如果你的板子用户名不是 `root`，比如是 `topeet`，就改成：

```bash
scp -r dist/iotgw_package topeet@192.168.31.238:/home/topeet/
```

### 方式 B：通过挂载或共享目录传输

如果你已经把板子目录挂载到了 Linux 虚拟机，比如挂载点是：

```text
/mnt/rk3568
```

那可以直接复制：

```bash
cd ~/IoTEdgeGateway

rm -rf /mnt/rk3568/iotgw_package
cp -r dist/iotgw_package /mnt/rk3568/
sync
```

这种方式也可以，不需要传整个项目源码，只传 `iotgw_package`。

## 8. 在 RK3568 板子上启动 MQTT

登录 RK3568：

```bash
ssh root@192.168.31.238
```

先确认板子上有 Mosquitto：

```bash
which mosquitto
which mosquitto_pub
which mosquitto_sub
```

如果能看到路径，说明 MQTT Broker 和测试工具都在。

先停掉可能残留的旧 Mosquitto，再启动新的 Mosquitto。推荐用下面这个配置，允许单片机和 PC 从局域网连接到板子的 `1883` 端口：

```bash
killall mosquitto 2>/dev/null || true

cat > /tmp/mosquitto-iotgw.conf <<'EOF'
listener 1883 0.0.0.0
allow_anonymous true
EOF

mosquitto -c /tmp/mosquitto-iotgw.conf -d
```

确认 `1883` 已经监听：

```bash
netstat -lntp 2>/dev/null | grep 1883
killall iotgw_gateway 2>/dev/null

```

正常应该看到类似：

```text
tcp        0      0 0.0.0.0:1883        0.0.0.0:*        LISTEN
```

如果你想看 MQTT 详细连接日志，不要用 `-d` 后台运行，改成前台运行：

```bash
killall mosquitto 2>/dev/null || true

mosquitto -c /tmp/mosquitto-iotgw.conf -v
```

这个终端不要关，另开一个 SSH 终端启动网关。

可以先手动测试一下 MQTT：

一个终端监听：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/#' -v
```

另一个终端发布：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/temp -m '{"value":26.5}'
```

如果监听终端看到：

```text
iotgw/dev/telemetry/temp {"value":26.5}
```

说明 MQTT 正常。

## 9. 在 RK3568 板子上启动网关

先停掉可能残留的旧网关进程：

```bash
killall iotgw_gateway 2>/dev/null || true
```

进入运行目录：

```bash
cd /opt/iotgw_package
```

给脚本和程序加执行权限：

```bash
chmod +x start.sh
chmod +x bin/iotgw_gateway
```

启动：

```bash
./start.sh
```

正常会看到类似：

```text
[INFO] Starting IoT Edge Gateway 1.0.0
[INFO] Web server listening at http://0.0.0.0:8080
[INFO] MQTT connected
```

如果看到类似下面的 MQTT 连接失败，说明 Mosquitto 没启动，或者 `1883` 没监听：

```text
MQTT connection closed
socket error
```

这时先回到第 8 步启动 Mosquitto，再重启网关。

最稳的恢复顺序是：

```bash
killall mosquitto 2>/dev/null || true
killall iotgw_gateway 2>/dev/null || true

cat > /tmp/mosquitto-iotgw.conf <<'EOF'
listener 1883 0.0.0.0
allow_anonymous true
EOF

mosquitto -c /tmp/mosquitto-iotgw.conf -d
netstat -lntp 2>/dev/null | grep 1883

cd /opt/iotgw_package
./start.sh
```

如果 `netstat` 没有任何输出，就不要启动网关，先把 Mosquitto 启动问题解决。网关日志里出现 `mqtt://127.0.0.1:1883 socket error`，本质就是网关连不上板子本机的 Mosquitto。

如果只是看网页和视频，暂时可以不管 MQTT；但如果要联调单片机实时数据和外设控制，必须看到：

```text
MQTT connected
```

## 10. PC 浏览器打开网页

在你的电脑浏览器里打开：

```text
http://192.168.31.238:8080/
```

注意：这里一定要用板子的 IP 地址，不要打开本地的：

```text
file:///.../www/index.html
```

也不要用：

```text
http://127.0.0.1:8080/
```

`127.0.0.1` 代表当前电脑自己，不代表 RK3568 板子。

## 11. 视频功能上板前检查

如果你编译的是 GStreamer 版，板子上还要确认运行环境有这些东西。

检查摄像头设备：

```bash
ls -l /dev/video*
```

当前项目默认用：

```text
/dev/video9
```

如果你的摄像头不是 `/dev/video9`，修改板子上的：

```text
/opt/iotgw_package/config/environments/rk3568.yaml
```

把：

```yaml
camera:
  device: "/dev/video9"
```

改成实际设备，比如：

```yaml
camera:
  device: "/dev/video0"
```

确认视频目录能创建：

```bash
mkdir -p /mnt/www/stream
mkdir -p /userdata/www/media
```

确认 ffmpeg 存在：

```bash
ffmpeg -version
```

抓拍和录像依赖 `ffmpeg`：

```text
/userdata/www/media/snapshot_YYYYMMDD_HHMMSS_N.jpg
/userdata/www/media/record_YYYYMMDD_HHMMSS_N.mp4
```

确认 GStreamer 插件：

```bash
gst-inspect-1.0 v4l2src
gst-inspect-1.0 mpph264enc
gst-inspect-1.0 hlssink
```

如果某个命令报找不到，说明板子运行环境缺对应插件。程序可能能启动，但点击“开启视频”会失败。

## 12. 视频功能保存位置

网页按钮对应关系如下：

```text
开启视频   -> POST /api/camera/start_stream
关闭视频   -> POST /api/camera/stop_stream
抓拍照片   -> POST /api/camera/snapshot
视频录制   -> POST /api/camera/start_record 或 /api/camera/stop_record
```

文件保存位置：

```text
HLS 切片: /mnt/www/stream/*.ts
HLS 播放列表: /mnt/www/stream/stream.m3u8
抓拍照片: /userdata/www/media/snapshot_YYYYMMDD_HHMMSS_N.jpg
录像文件: /userdata/www/media/record_YYYYMMDD_HHMMSS_N.mp4
```

网页访问地址：

```text
http://RK3568_IP:8080/stream/stream.m3u8
http://RK3568_IP:8080/media/snapshot_YYYYMMDD_HHMMSS_N.jpg
http://RK3568_IP:8080/media/record_YYYYMMDD_HHMMSS_N.mp4
```

## 13. 低延迟视频参数

当前项目使用 HLS 播放视频。HLS 比 WebRTC 稳定、部署简单，但天然会有切片缓冲延迟。

当前稳定低延迟方案是：

```text
后端 hlssink: target-duration=1 max-files=5
前端 HLS.js: liveSyncDurationCount=1, liveMaxLatencyDurationCount=3
```

这套参数已经把延迟从约 7-8 秒压到约 2.5 秒，同时保持了较好的播放稳定性。

如果后续发现画面卡顿，优先把前端 `liveSyncDurationCount` 改回默认，也就是把 `www/index.html` 里的 `new Hls({...})` 改回：

```js
hlsInstance = new Hls();
```

如果后续必须接近 1 秒以内，就不建议继续硬压 HLS 参数了，可以再评估 WebRTC、RTSP 低延迟播放器，或者 MJPEG 预览流。

## 14. MQTT 单片机实时数据联调

当前网关配置会连接 RK3568 本机 MQTT Broker：

```yaml
mqtt:
  enabled: true
  broker_host: "127.0.0.1"
  broker_port: 1883
  topic_prefix: "iotgw/dev/"
```

因此推荐数据链路是：

```text
单片机 publish -> RK3568 Mosquitto -> iotgw_gateway subscribe -> /api/status -> 前端显示
```

先在 RK3568 上确认 Mosquitto：

```bash
which mosquitto
which mosquitto_pub
which mosquitto_sub
```

启动 MQTT Broker：

```bash
mosquitto -p 1883 -d
netstat -lntp 2>/dev/null | grep 1883
```

启动网关后端后，终端应看到：

```text
[INFO] MQTT connected
[INFO] MQTT subscribed iotgw/dev/#
```

单片机发布传感器数据时，使用这些主题：

```text
iotgw/dev/telemetry/temp
iotgw/dev/telemetry/humi
iotgw/dev/telemetry/light
iotgw/dev/telemetry/ir
```

Payload 使用 JSON，字段名固定为 `value`：

```json
{"value":25.6}
```

手动模拟测试：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/temp -m '{"value":26.5}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/humi -m '{"value":58}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/light -m '{"value":760}'
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/ir -m '{"value":1}'
```

前端网页会通过 `/api/status` 刷新数据。也可以直接在浏览器打开：

```text
http://RK3568_IP:8080/api/status
```

单片机 MQTT 客户端建议配置：

```text
Broker Host: RK3568_IP
Broker Port: 1883
Client ID: mcu_sensor_001
Username: 空
Password: 空
QoS: 0
Retain: false
```

如果网页不更新，先在 RK3568 上监听：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'iotgw/dev/#' -v
```

能看到单片机发来的数据，说明 MQTT 到板子没问题；再检查 `iotgw_gateway` 是否显示 `MQTT connected`。

## 15. ZigBee DL-20 透明串口联调

当前 ZigBee 默认配置：

```yaml
zigbee:
  enabled: true
  device: "/dev/ttyS4"
  baudrate: 9600
```

DL-20 使用透明串口传输，串口参数为：

```text
9600 8N1，无流控，3.3V TTL
```

启动网关后，可以先看 ZigBee 状态：

```text
http://RK3568_IP:8080/api/zigbee/status
```

正常示例：

```json
{"ok":true,"open":true,"device":"/dev/ttyS4","baudrate":9600}
```

如果 `open:false`，说明 `/dev/ttyS4` 没打开成功，先检查串口是否接错或被占用。

手动测试串口：

```bash
stty -F /dev/ttyS4 9600 cs8 -cstopb -parenb -ixon -ixoff -crtscts
cat /dev/ttyS4
```

前端选择 `ZigBee` 后，LED、电机、蜂鸣器命令会通过 `/dev/ttyS4` 发出一行 JSON。

ZigBee 接收传感器数据时，单片机发送：

```json
{"type":"telemetry","id":"temp","value":25.6}
```

每条 JSON 末尾必须带换行 `\n`。网关收到后会复用现有 `/api/status` 状态刷新逻辑。

## 16. 常见问题

### 16.1 板子上网页打不开

在板子上检查 8080 是否监听：

```bash
ss -lntp | grep 8080
```

如果没有输出，说明程序没启动成功。

如果有输出，再确认 PC 能 ping 通板子：

```bash
ping 192.168.31.238
```

还要确认浏览器访问的是：

```text
http://板子IP:8080/
```

不是 `127.0.0.1`。

### 16.2 启动时报 MQTT 错误

如果你暂时不用 MQTT，可以先忽略。网页和视频服务仍然能启动。

如果想暂时关闭 MQTT，可以修改：

```text
config/environments/rk3568.yaml
```

把：

```yaml
mqtt:
  enabled: true
```

改成：

```yaml
mqtt:
  enabled: false
```

然后重新打包或直接修改板子上 `/opt/iotgw_package/config/environments/rk3568.yaml`。

### 16.3 点击开启视频失败

按顺序检查：

```bash
ls -l /dev/video*
ffmpeg -version
gst-inspect-1.0 v4l2src
gst-inspect-1.0 mpph264enc
gst-inspect-1.0 hlssink
ls -ld /mnt/www/stream /userdata/www
```

再看程序终端日志里有没有 GStreamer pipeline 的报错。

### 16.4 编译时报找不到 gstreamer-1.0

当前项目已经改成运行时调用 `gst-launch-1.0`，正常不会再因为缺少 `gstreamer-1.0.pc` 导致 CMake 失败。

如果你看到旧版错误：

```text
Package 'gstreamer-1.0', required by 'virtual:world', not found
```

说明你手里的代码还是旧版本，里面还在直接链接 GStreamer 开发库。请确认项目里的 `CMakeLists.txt` 没有下面这些内容：

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
```

现在新版不需要它们。重新同步代码后，再执行：

```bash
rm -rf build-rk3568
cmake -S . -B build-rk3568 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3568-buildroot.cmake \
  -DIOTGW_ENABLE_GSTREAMER=ON
cmake --build build-rk3568 -j"$(nproc)"
```

## 17. 每次修改代码后的固定操作

以后你每次改完项目，基本就按这几步走。这个流程会同时更新后端程序、`www/index.html` 前端页面、配置文件。

先确认当前源码是新版：

```bash
cd ~/IoTEdgeGateway

# 新版视频后端不再依赖 gstreamer-1.0.pc，这里应该没有输出
grep -n "pkg_check_modules" CMakeLists.txt

# 新版后端通过运行时调用 gst-launch-1.0 启动视频流
grep -n "gst-launch-1.0" src/services/system_services/camera/camera_manager.cpp

# 新版前端摄像头控制接口必须使用 POST
grep -n "API_START_STREAM, { method: 'POST' }" www/index.html
```

如果第一条有输出，或者后两条没有输出，说明 Linux 虚拟机里的源码还不是最新的，先把 Windows/主机上的项目同步到 Linux 虚拟机。

确认无误后重新编译：

```bash
cd ~/IoTEdgeGateway

rm -rf build-rk3568

cmake -S . -B build-rk3568 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3568-buildroot.cmake \
  -DIOTGW_ENABLE_GSTREAMER=ON

cmake --build build-rk3568 -j"$(nproc)"

file build-rk3568/iotgw_gateway
strings build-rk3568/iotgw_gateway | grep start_stream
strings build-rk3568/iotgw_gateway | grep gst-launch

rm -rf dist/iotgw_package
./scripts/package_runtime.sh build-rk3568 dist/iotgw_package

strings dist/iotgw_package/bin/iotgw_gateway | grep start_stream
strings dist/iotgw_package/bin/iotgw_gateway | grep gst-launch

scp -r dist/iotgw_package root@192.168.31.238:/opt/
```

然后在板子上先停旧进程，再替换或启动：

```bash
killall iotgw_gateway 2>/dev/null || true

cd /opt/iotgw_package
chmod +x start.sh bin/iotgw_gateway
./start.sh
```

PC 浏览器打开：

```text
http://192.168.31.238:8080/
```

启动后可以确认 SQLite 数据库：

```bash
ls -lh /opt/iotgw_package/data/iotgw.db*
sqlite3 /opt/iotgw_package/data/iotgw.db ".tables"
```

详细数据库查看方法见：

```text
docs/sqlite-database-usage.md
```

如果浏览器之前已经打开过页面，按 `Ctrl + F5` 强制刷新，避免浏览器还用旧的 `index.html` 缓存。

如果点击“开启视频”仍失败，先看错误类型：

```text
HTTP 404
```

通常表示前端还在发 GET、后端不是新版、或者浏览器缓存旧页面。确认：

```bash
grep -n "API_START_STREAM, { method: 'POST' }" /opt/iotgw_package/www/index.html
strings /opt/iotgw_package/bin/iotgw_gateway | grep start_stream
```

如果不是 404，而是 `stream_start_failed`，说明接口已经进入后端视频逻辑，继续在板子上看：

```bash
cat /tmp/iotgw_gst.log
ls -lh /mnt/www/stream
```
