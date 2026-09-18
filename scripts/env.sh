#!/usr/bin/env bash
# Environment setup for ESPirate Zephyr build

export ZEPHYR_BASE="${ZEPHYR_BASE:-/home/alan/zephyr-projects/zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-/home/alan/zephyr-sdk-1.0.1}"

# Activate python virtual environment with west & esptool
if [ -d "$HOME/.esptoolenv" ]; then
    source "$HOME/.esptoolenv/bin/activate"
fi

# Ensure west can find Zephyr installation
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr

# Auto-detect target serial port if not explicitly set
if [ -z "${PORT}" ]; then
    if [ -e /dev/ttyACM0 ]; then
        export PORT="/dev/ttyACM0"
    elif [ -e /dev/ttyUSB0 ]; then
        export PORT="/dev/ttyUSB0"
    else
        export PORT="/dev/ttyACM0"
    fi
fi

echo "ESPirate environment configured:"
echo "  ZEPHYR_BASE:            $ZEPHYR_BASE"
echo "  ZEPHYR_SDK_INSTALL_DIR: $ZEPHYR_SDK_INSTALL_DIR"
echo "  Target PORT:            $PORT"
echo "  Python / west:          $(which west 2>/dev/null || echo 'not found')"
