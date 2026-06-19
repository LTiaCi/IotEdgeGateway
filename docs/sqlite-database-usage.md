# SQLite 数据库记录与查看说明

这份文档说明 RK3568 网关如何把采集数据、照片、录像、控制命令和运行日志写入 SQLite，方便后续在网页或命令行查看。

## 1. 数据库位置

当前 RK3568 配置文件：

```text
config/environments/rk3568.yaml
```

数据库配置：

```yaml
database:
  path: data/iotgw.db
```

所以程序在板子上运行时，数据库文件会自动生成在：

```text
/opt/iotgw_package/data/iotgw.db
```

同时 SQLite WAL 模式会生成两个辅助文件，这是正常现象：

```text
/opt/iotgw_package/data/iotgw.db-wal
/opt/iotgw_package/data/iotgw.db-shm
```

## 2. 已记录的数据

### 2.1 传感器采集数据

单片机通过 MQTT 发来的实时数据会写入：

```text
sensor_samples
```

字段：

```text
id         自增 ID
ts_ms      时间戳，毫秒
device_id  传感器 ID，例如 temp、humi、light、ir
topic      MQTT topic
value      解析后的数值
payload    原始 MQTT payload
```

### 2.2 抓拍照片和录像

网页点击“抓拍照片”“视频录制”后，文件信息会写入：

```text
media_files
```

字段：

```text
id          自增 ID
ts_ms       时间戳，毫秒
media_type  snapshot、record_start、record
url         网页访问路径，例如 /media/snapshot_20260618_101010_1.jpg
file_path   板子文件路径，例如 /userdata/www/media/snapshot_20260618_101010_1.jpg
message     后端返回消息
mime_type   image/jpeg、video/mp4 等
content_size 写入数据库的文件大小，单位字节
content     图片或视频文件内容，BLOB
```

注意：现在数据库会保存照片和录像文件内容，同时仍保留原文件路径和 URL。这样即使原文件还在 `/userdata/www/media/`，也可以从 SQLite 里直接取出对应图片或视频。

录像文件会明显增大 SQLite 数据库体积，板子空间紧张时要定期清理旧录像记录，或者减少录像时长。

### 2.3 外设控制命令

网页下发 LED、电机、蜂鸣器命令时，会写入：

```text
actuator_commands
```

字段：

```text
id         自增 ID
ts_ms      时间戳，毫秒
device_id  led、motor、buzzer
topic      MQTT command topic
payload    实际发出的 JSON
ok         1 表示已成功发布到 MQTT，0 表示发布失败
```

### 2.4 运行日志

后端运行日志会写入：

```text
runtime_logs
```

字段：

```text
id       自增 ID
ts_ms    时间戳，毫秒
level    INFO、WARN、ERROR 等
message  日志内容
```

为了避免数据库无限增长，运行日志只保留最近 2000 条。传感器、媒体、控制命令暂时不自动删除。

## 3. 网页查看

重新编译、打包、上板运行后，网页左侧会多一个：

```text
数据库记录
```

里面有四个页签：

```text
传感器
照片/视频
控制命令
运行日志
```

网页会自动调用这些接口：

```text
GET /api/db/summary
GET /api/db/sensors?limit=20
GET /api/db/media?limit=20
GET /api/db/commands?limit=20
GET /api/db/logs?limit=20
```

每 3 秒刷新一次最近记录。

照片/视频页签里：

```text
数据库打开  从 SQLite BLOB 读取图片或视频
文件打开    从 /userdata/www/media 原文件路径读取图片或视频
```

## 4. API 查看

在 PC 浏览器直接访问：

```text
http://192.168.31.238:8080/api/db/summary
http://192.168.31.238:8080/api/db/sensors?limit=5
http://192.168.31.238:8080/api/db/media?limit=5
http://192.168.31.238:8080/api/db/commands?limit=5
http://192.168.31.238:8080/api/db/logs?limit=5
```

如果板子 IP 不是 `192.168.31.238`，换成实际 IP。

## 5. 板子命令行查看

登录板子：

```bash
ssh root@192.168.31.238
cd /opt/iotgw_package
```

查看表：

```bash
sqlite3 data/iotgw.db ".tables"
```

查看最近 5 条传感器数据：

```bash
sqlite3 data/iotgw.db "select id,datetime(ts_ms/1000,'unixepoch','localtime'),device_id,value,topic,payload from sensor_samples order by ts_ms desc limit 5;"
```

查看最近 5 条照片/录像记录：

```bash
sqlite3 data/iotgw.db "select id,datetime(ts_ms/1000,'unixepoch','localtime'),media_type,content_size,url,file_path from media_files order by ts_ms desc limit 5;"
```

查看数据库里媒体内容总占用：

```bash
sqlite3 data/iotgw.db "select sum(content_size) from media_files;"
```

查看最近 5 条控制命令：

```bash
sqlite3 data/iotgw.db "select id,datetime(ts_ms/1000,'unixepoch','localtime'),device_id,ok,topic,payload from actuator_commands order by ts_ms desc limit 5;"
```

查看最近 20 条运行日志：

```bash
sqlite3 data/iotgw.db "select id,datetime(ts_ms/1000,'unixepoch','localtime'),level,message from runtime_logs order by ts_ms desc limit 20;"
```

## 6. 编译前确认 SQLite 开发文件

板子上有 `sqlite3` 命令和 `libsqlite3.so` 只代表运行环境有 SQLite。

交叉编译时，Linux 虚拟机的 Buildroot sysroot 里也要有：

```text
sqlite3.h
libsqlite3.so
```

在 Linux 虚拟机里检查：

```bash
cd ~/source/rk356x_linux/buildroot

find output/host/aarch64-buildroot-linux-gnu/sysroot -name "sqlite3.h"
find output/host/aarch64-buildroot-linux-gnu/sysroot -name "libsqlite3.so*"
```

正常应该能看到类似：

```text
output/host/aarch64-buildroot-linux-gnu/sysroot/usr/include/sqlite3.h
output/host/aarch64-buildroot-linux-gnu/sysroot/usr/lib/libsqlite3.so
```

如果找不到，CMake 会直接报：

```text
SQLite3 development files were not found in the toolchain/sysroot.
```

这种情况需要在 Buildroot 里启用 SQLite 开发库，或者把带 `sqlite3.h` 和 `libsqlite3.so` 的 sysroot 配好后再编译。

## 7. 重新编译打包

在 Linux 虚拟机项目目录：

```bash
cd ~/IoTEdgeGateway

rm -rf build-rk3568

cmake -S . -B build-rk3568 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3568-buildroot.cmake \
  -DIOTGW_ENABLE_GSTREAMER=ON

cmake --build build-rk3568 -j"$(nproc)"

rm -rf dist/iotgw_package
./scripts/package_runtime.sh build-rk3568 dist/iotgw_package

scp -r dist/iotgw_package root@192.168.31.238:/opt/
```

板子上启动：

```bash
ssh root@192.168.31.238

killall iotgw_gateway 2>/dev/null || true

cd /opt/iotgw_package
chmod +x start.sh bin/iotgw_gateway
./start.sh
```

启动后确认数据库生成：

```bash
ls -lh /opt/iotgw_package/data/iotgw.db*
sqlite3 /opt/iotgw_package/data/iotgw.db ".tables"
```

## 8. 快速验证

1. 打开网页：

```text
http://192.168.31.238:8080/
```

2. 等单片机发 MQTT 传感器数据，或手动模拟：

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t iotgw/dev/telemetry/temp -m '{"value":26.5}'
```

3. 网页“数据库记录 -> 传感器”应该能看到新记录。

4. 点击 LED、电机、蜂鸣器控制后，“数据库记录 -> 控制命令”应该能看到下发 JSON。

5. 点击抓拍照片后，“数据库记录 -> 照片/视频”应该能看到图片 URL、板子文件路径和数据库内容大小。

6. 点击“数据库打开”，应该能从 SQLite 里直接打开图片或视频。

7. 查看日志：

```bash
sqlite3 /opt/iotgw_package/data/iotgw.db "select level,message from runtime_logs order by ts_ms desc limit 10;"
```
