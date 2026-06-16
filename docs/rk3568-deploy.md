# RK3568 Deployment Guide

This project should run the gateway backend on the RK3568 board. The PC browser
then opens the web page served by the board, not the local `file://` copy.

## 1. Runtime Topology

```text
MCU / ESP32 / sensor node
        |
        | MQTT
        v
Mosquitto MQTT broker
        |
        v
RK3568 iotgw_gateway backend
        |
        | HTTP + WebSocket
        v
PC browser: http://RK3568_IP:8080/
```

## 2. Install Dependencies On RK3568

```bash
sudo apt update
sudo apt install -y cmake g++ git mosquitto mosquitto-clients
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

Check MQTT broker status:

```bash
systemctl status mosquitto
```

## 3. Cross Build In Linux VM

Use this route when your Linux VM already has the RK356x SDK and ARM64
cross-compiler.

Find your compiler first:

```bash
find /path/to/rk356x_linux -name '*aarch64*g++' 2>/dev/null
```

Copy the toolchain template:

```bash
cp cmake/toolchains/rk3568-aarch64.template.cmake \
   cmake/toolchains/rk3568-aarch64.cmake
```

Edit `cmake/toolchains/rk3568-aarch64.cmake` and set:

```cmake
set(CMAKE_C_COMPILER /absolute/path/to/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /absolute/path/to/aarch64-linux-gnu-g++)
```

If your SDK has a board-matched sysroot, also set:

```cmake
set(CMAKE_SYSROOT /absolute/path/to/sysroot)
```

Mongoose is already vendored in `third_party/mongoose`, so no extra Mongoose
setup is required. Configure and build:

```bash
cmake -S . -B build-rk3568 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3568-aarch64.cmake

cmake --build build-rk3568 -j"$(nproc)"
```

Camera/HLS support is optional. Keep it off while validating MQTT and the web
dashboard. If your sysroot has GStreamer development files and pkg-config
metadata, enable it with:

```bash
export PKG_CONFIG_SYSROOT_DIR=/opt/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/aarch64-linux-gnu/libc
export PKG_CONFIG_LIBDIR=$PKG_CONFIG_SYSROOT_DIR/usr/lib/pkgconfig:$PKG_CONFIG_SYSROOT_DIR/usr/share/pkgconfig

cmake -S . -B build-rk3568 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3568-aarch64.cmake \
  -DIOTGW_ENABLE_GSTREAMER=ON

cmake --build build-rk3568 -j"$(nproc)"
```

The default camera config uses `/dev/video9`, writes HLS segments to
`/mnt/www/stream`, and stores `snapshot.jpg` / `record.mp4` in `/userdata/www`.

Confirm the output is ARM64:

```bash
file build-rk3568/iotgw_gateway
```

Expected output should include:

```text
ELF 64-bit ... ARM aarch64
```

Then create the runtime package:

```bash
./scripts/package_runtime.sh build-rk3568 dist/iotgw_package
```

Copy only `dist/iotgw_package` to RK3568.

## 4. Optional: Build Directly On RK3568

Put the project on the board, for example:

```bash
cd /home/linaro/IoTEdgeGateway
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Then package it:

```bash
./scripts/package_runtime.sh build dist/iotgw_package
```

## 5. Start Gateway

The RK3568-specific config is:

```text
config/environments/rk3568.yaml
```

It enables MQTT and listens on `0.0.0.0:8080`.

Start with:

```bash
cd /path/to/iotgw_package
./start.sh
```

If you are starting from the full source tree instead of the runtime package:

```bash
./build/iotgw_gateway \
  --yaml-config config/environments/rk3568.yaml \
  --log-level info
```

Confirm the web server is listening:

```bash
ss -lntp | grep 8080
```

## 6. Open From PC

Find the board IP:

```bash
ip addr
```

If the RK3568 IP is `192.168.1.88`, open this on the PC:

```text
http://192.168.1.88:8080/
```

Do not use:

```text
file:///.../www/index.html
```

The `file://` page cannot connect to the gateway APIs and WebSocket correctly.

## 7. Test MQTT Without MCU

On RK3568:

```bash
./scripts/rk3568/test_mqtt.sh 127.0.0.1
```

This publishes sample telemetry:

```text
iotgw/dev/telemetry/temp
iotgw/dev/telemetry/humi
iotgw/dev/telemetry/light
iotgw/dev/telemetry/ir
```

The PC web page should update after the gateway receives these MQTT messages.

If you copied only `iotgw_package`, the test script is not included by default.
Run it from the source tree or copy the script separately.

## 8. MCU / ESP32 Telemetry Contract

If the MQTT broker runs on RK3568, configure the MCU MQTT client as:

```text
host: RK3568_IP
port: 1883
```

Publish sensor data to:

```text
iotgw/dev/telemetry/temp
iotgw/dev/telemetry/humi
iotgw/dev/telemetry/light
iotgw/dev/telemetry/ir
```

Payload format:

```json
{"device_id":"temp","type":"sensor","data":{"value":26.5},"ts":1710000000}
```

## 9. Actuator Command Topics

The gateway publishes commands to:

```text
iotgw/dev/command/led
iotgw/dev/command/motor
iotgw/dev/command/buzzer
```

The MCU should subscribe to:

```text
iotgw/dev/command/#
```

Buzzer command examples:

```json
{"device_id":"buzzer","type":"cmd","data":{"on":true},"ts":1710000000}
```

```json
{"device_id":"buzzer","type":"cmd","data":{"on":false},"ts":1710000000}
```
