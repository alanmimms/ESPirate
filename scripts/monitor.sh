#!/usr/bin/env bash
# Open serial monitor to ESPirate target
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

BAUD="${BAUD:-115200}"

echo "Connecting serial monitor to ${PORT} at ${BAUD} baud..."

if command -v tio >/dev/null 2>&1; then
    exec tio -b "${BAUD}" "${PORT}"
elif command -v picocom >/dev/null 2>&1; then
    exec picocom -b "${BAUD}" "${PORT}"
elif command -v minicom >/dev/null 2>&1; then
    exec minicom -D "${PORT}" -b "${BAUD}"
else
    echo "No terminal emulator (tio, picocom, minicom) found."
    exit 1
fi
