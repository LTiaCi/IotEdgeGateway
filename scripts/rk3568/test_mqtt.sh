#!/usr/bin/env bash
set -euo pipefail

HOST="${1:-127.0.0.1}"
PREFIX="${2:-iotgw/dev}"

if ! command -v mosquitto_pub >/dev/null 2>&1; then
  echo "mosquitto_pub is required. Install it with:"
  echo "  sudo apt install -y mosquitto-clients"
  exit 1
fi

publish_sensor() {
  local id="$1"
  local value="$2"
  local ts
  ts="$(date +%s)"
  mosquitto_pub -h "${HOST}" -t "${PREFIX}/telemetry/${id}" \
    -m "{\"device_id\":\"${id}\",\"type\":\"sensor\",\"data\":{\"value\":${value}},\"ts\":${ts}}"
}

echo "Publishing sample telemetry to MQTT broker ${HOST}, prefix ${PREFIX}"
publish_sensor temp 26.5
publish_sensor humi 58
publish_sensor light 420
publish_sensor ir 1
echo "Done."
