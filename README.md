# IoT Edge Gateway

C++14 IoT edge gateway course project. It provides a single-process event loop,
REST APIs, WebSocket push, MQTT protocol adapter, device registry, rule engine,
YAML-style configuration, file logging, and a static control dashboard.

The project intentionally does not ship with any Aliyun MQTT endpoint. MQTT is
disabled by default in development config; set your own broker if you need it.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

If Ninja is not installed, use your platform's default generator:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Run

```bash
./build/iotgw_gateway --yaml-config config/environments/development.yaml --log-level debug
```

Windows example:

```powershell
.\build\Debug\iotgw_gateway.exe --yaml-config config\environments\development.yaml --log-level debug
```

## APIs

- `GET /api/health` -> `{"status":"ok"}`
- `GET /api/version` -> `{"version":"1.0.0"}`
- `GET /api/devices`
- `GET /api/devices/<id>`
- `POST /api/actuators/<id>/set`
- `GET /api/rules`
- `POST /api/rules/reload`
- `POST /api/rules/<id>/enable`
- `POST /api/rules/<id>/disable`
- `GET /api/status`
- `POST /api/control`

Open `http://localhost:8080/` for the control dashboard.

## RK3568 Deployment

For the real board workflow, run the backend on RK3568 and open it from the PC:

```text
http://<RK3568_IP>:8080/
```

Use the board config:

```bash
./build/iotgw_gateway --yaml-config config/environments/rk3568.yaml --log-level info
```

See `docs/rk3568-deploy.md` for MQTT broker setup, build commands, PC access,
and MCU telemetry topics.
