#!/usr/bin/env bash
# Copyright (c) 2026 Alan Mimms / ESPirate
# SPDX-License-Identifier: Apache-2.0

# push_web.sh: Fast wireless upload of static files (HTML, CSS, JS, etc.)
# directly into LittleFS on the ESPirate target in milliseconds.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

FILE="${1:-${PROJECT_DIR}/web/index.html}"
NAME="$(basename "${FILE}")"
TARGET="${TARGET:-192.168.0.54}"

if [ ! -f "${FILE}" ]; then
    echo "Error: File not found: ${FILE}"
    exit 1
fi

SIZE=$(wc -c < "${FILE}")

echo "Pushing ${NAME} (${SIZE} bytes) to http://${TARGET}/api/upload?name=${NAME}..."
START_TIME=$(date +%s%N)

RESP=$(curl -s -X POST --data-binary @"${FILE}" "http://${TARGET}/api/upload?name=${NAME}")

END_TIME=$(date +%s%N)
ELAPSED_MS=$(( (END_TIME - START_TIME) / 1000000 ))

echo "Response: ${RESP}"
echo "✓ Upload completed in ${ELAPSED_MS} ms! Refresh your browser to see changes."
