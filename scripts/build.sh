#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

source "${SCRIPT_DIR}/env.sh"

BOARD="${BOARD:-esp32s3_devkitc/esp32s3/procpu}"

echo "Building ESPirate for target board: ${BOARD}"
cd "${PROJECT_DIR}"

west build -b "${BOARD}" . "$@"
