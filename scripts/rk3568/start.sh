#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${PROJECT_ROOT}"

BIN="${PROJECT_ROOT}/build/iotgw_gateway"
CONFIG="${PROJECT_ROOT}/config/environments/rk3568.yaml"

if [[ ! -x "${BIN}" ]]; then
  echo "iotgw_gateway not found at ${BIN}"
  echo "Build it first:"
  echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
  echo "  cmake --build build -j\$(nproc)"
  exit 1
fi

exec "${BIN}" --yaml-config "${CONFIG}" --log-level info
