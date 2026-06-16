#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build-rk3568}"
PACKAGE_DIR="${2:-dist/iotgw_package}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${PROJECT_ROOT}"

BIN="${BUILD_DIR}/iotgw_gateway"
if [[ ! -x "${BIN}" ]]; then
  echo "Gateway binary not found or not executable: ${BIN}"
  echo "Usage:"
  echo "  ./scripts/package_runtime.sh <build-dir> [package-dir]"
  echo "Example:"
  echo "  ./scripts/package_runtime.sh build-rk3568 dist/iotgw_package"
  exit 1
fi

rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}/bin"
mkdir -p "${PACKAGE_DIR}/config"
mkdir -p "${PACKAGE_DIR}/www"
mkdir -p "${PACKAGE_DIR}/data/logs"
mkdir -p "${PACKAGE_DIR}/data/media"

cp "${BIN}" "${PACKAGE_DIR}/bin/iotgw_gateway"
cp -r config/* "${PACKAGE_DIR}/config/"
cp -r www/* "${PACKAGE_DIR}/www/"

cat > "${PACKAGE_DIR}/start.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

exec ./bin/iotgw_gateway \
  --yaml-config config/environments/rk3568.yaml \
  --log-level info
EOF

chmod +x "${PACKAGE_DIR}/start.sh"
chmod +x "${PACKAGE_DIR}/bin/iotgw_gateway"

echo "Runtime package created: ${PACKAGE_DIR}"
echo "Copy this directory to RK3568 and run:"
echo "  cd ${PACKAGE_DIR##*/}"
echo "  ./start.sh"
