#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

source "${SCRIPT_DIR}/env.sh"

echo "Flashing ESPirate to target device on ${PORT}..."
cd "${PROJECT_DIR}"

west flash --esp-device "${PORT}" "$@"
