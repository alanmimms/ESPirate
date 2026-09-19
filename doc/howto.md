# ESPirate How-To Guide & API Reference

ESPirate integrates a persistent **Lua 5.5** scripting engine, an active web dashboard, and hardware peripheral drivers into the Zephyr RTOS on the **ESP32-S3**. This document is the comprehensive guide for building, flashing, monitoring, managing flash storage, transferring files, and scripting hardware controllers (GPIO, Matrix, PWM, I2C, SPI, UART).

---

## Table of Contents
1. [Environment Setup](#1-environment-setup)
2. [Building Firmware](#2-building-firmware)
3. [Flashing & Hardware Erase](#3-flashing--hardware-erase)
4. [Serial Console Monitoring](#4-serial-console-monitoring)
5. [LittleFS Storage Management & Erasing](#5-littlefs-storage-management--erasing)
6. [Copying Files To/From LittleFS with cURL](#6-copying-files-tofrom-littlefs-with-curl)
7. [REST API Reference & Execution Endpoints](#7-rest-api-reference--execution-endpoints)
8. [Hardware Controller Scripting & Examples](#8-hardware-controller-scripting--examples)
   - [GPIO Subsystem (`gpio`)](#gpio-subsystem-gpio)
   - [Silicon Matrix Pin Muxing (`matrix`)](#silicon-matrix-pin-muxing-matrix)
   - [PWM Subsystem (`pwm`)](#pwm-subsystem-pwm)
   - [I2C Subsystem (`i2c`)](#i2c-subsystem-i2c)
   - [SPI Subsystem (`spi`)](#spi-subsystem-spi)
   - [UART Subsystem (`uart`)](#uart-subsystem-uart)
   - [System & Timing Primitives (`sys`)](#system--timing-primitives-sys)
   - [Telemetry & Results Reporting (`telemetry`)](#telemetry--results-reporting-telemetry)
9. [Complete Hardware Test Script Example](#9-complete-hardware-test-script-example)

---

## 1. Environment Setup

ESPirate uses the Zephyr RTOS v4.2+ toolchain with the Zephyr SDK and Python virtual environment.

Source the project environment:
```bash
cd ~/ha/ESPirate
source scripts/env.sh
```

This exports:
* `ZEPHYR_BASE`: Path to the Zephyr kernel tree.
* `ZEPHYR_SDK_INSTALL_DIR`: Path to the Xtensa toolchain.
* `PORT`: Default serial device (`/dev/ttyACM0`).

---

## 2. Building Firmware

Build the ESPirate application:
```bash
./scripts/build.sh
```

To perform a pristine clean rebuild:
```bash
./scripts/build.sh -p
```

Generated outputs under `build/zephyr/`:
* `build/zephyr/zephyr.bin`: Raw flash binary image (~1 MB).
* `build/zephyr/zephyr.elf`: ELF binary with debug symbols.

---

## 3. Flashing & Hardware Erase

### Standard Flash
Flash the compiled binary to the ESP32-S3:
```bash
./scripts/flash.sh
```
*Note: `west flash` only writes the sectors occupied by the firmware binary (`0x000000`–`0x0F7FFF`). It does not disturb LittleFS storage or saved Wi-Fi credentials.*

### Complete Chip Erase
To erase the entire SPI flash (wiping all LittleFS storage partitions, Wi-Fi credentials, and settings):
```bash
./scripts/flash.sh --erase
```
*When the device boots after `--erase`, the LittleFS subsystem detects blank flash and automatically creates a fresh filesystem spanning 100% of the available storage.*

### Custom Serial Port
To target a different device port:
```bash
PORT=/dev/ttyACM1 ./scripts/flash.sh
```

---

## 4. Serial Console Monitoring

Connect to the interactive USB CDC-ACM console (115200 baud):
```bash
./scripts/monitor.sh
```

Or connect directly with `tio`:
```bash
tio -b 115200 /dev/ttyACM0
```
*(To exit `tio`, press `Ctrl-T` followed by `q`)*

Press `Enter` to access the prompt:
```text
ESPirate> help
ESPirate> espirate status
ESPirate> storage status
ESPirate> wifi status
```

---

## 5. LittleFS Storage Management & Erasing

ESPirate allocates a dedicated, non-volatile LittleFS flash partition that automatically detects the physical flash size at runtime (2 MB on 4 MB chips, 6 MB on 8 MB chips, 14 MB on 16 MB chips).

### Checking Storage Status
```bash
ESPirate> storage status
=== LittleFS Storage Subsystem ===
  Mount Point : /lfs
  State       : MOUNTED
  Total Space : 6144 KB (6291456 bytes)
  Used Space  : 16 KB (16384 bytes)
  Free Space  : 6128 KB (6275072 bytes)
  Partition   : Dynamic SPI Flash (0x200000 -> End of Flash)
```

### Listing Files
```bash
ESPirate> storage ls
Files in /lfs:
  Name                     Size (bytes)
  ------------------------ ----------
  demo.lua                        369
  test_gpio.lua                  2481
  ------------------------ ----------
  Total: 2 file(s), 2850 bytes
```
*(Standard Zephyr `fs ls /lfs` is also supported).*

### Viewing File Content
```bash
ESPirate> storage cat demo.lua
```

### Deleting a File
```bash
ESPirate> storage rm demo.lua
```

### Renaming a File
```bash
ESPirate> storage rename old_script.lua new_script.lua
```

### Erasing / Formatting LittleFS
You can format the storage partition at any time using any of the following methods:

1. **Via UART Shell:**
   ```bash
   ESPirate> storage format
   ```
2. **Via REST API (cURL):**
   ```bash
   curl -X POST http://espirate-cea0.local/api/storage/format
   ```
3. **Via Web Dashboard:**
   Click the **Refresh** button or use the Web API format trigger.
4. **Via esptool Hardware Erase:**
   ```bash
   ./scripts/flash.sh --erase
   ```

---

## 6. Copying Files To/From LittleFS with cURL

ESPirate exposes a REST API on port 80 over Wi-Fi. In the examples below, replace `espirate-cea0.local` with your device's mDNS domain or assigned IP address (`192.168.0.x` or `192.168.4.1`).

### A. List Files and Sizes
```bash
curl -s http://espirate-cea0.local/api/scripts | jq .
```
Response:
```json
[
  { "name": "demo.lua", "size": 369 },
  { "name": "test_gpio.lua", "size": 2481 }
]
```

### B. Download (Copy File FROM LittleFS)
Save a file from the ESP32 to your local workstation:
```bash
curl -s "http://espirate-cea0.local/api/script?name=demo.lua" -o demo.lua
```

### C. Upload (Copy File TO LittleFS)
Upload a local Lua script to the device's persistent flash:

#### Using `jq` to create the JSON payload:
```bash
jq -n --arg name "my_test.lua" --rawfile content my_test.lua \
  '{"name": $name, "content": $content}' | \
  curl -s -X POST http://espirate-cea0.local/api/script \
  -H "Content-Type: application/json" -d @-
```

#### Using a Python one-liner (no `jq` required):
```bash
python3 -c '
import sys, json, urllib.request
fn = sys.argv[1]
with open(fn, "r") as f: code = f.read()
payload = json.dumps({"name": fn, "content": code}).encode("utf-8")
req = urllib.request.Request("http://espirate-cea0.local/api/script", data=payload, headers={"Content-Type":"application/json"})
urllib.request.urlopen(req)
print("Uploaded", fn)
' my_test.lua
```

### D. Rename a File in LittleFS
```bash
curl -s -X POST http://espirate-cea0.local/api/script/rename \
  -H "Content-Type: application/json" \
  -d '{"old_name":"my_test.lua", "new_name":"production_test.lua"}'
```

### E. Delete a File in LittleFS
```bash
curl -s -X POST http://espirate-cea0.local/api/script/delete \
  -H "Content-Type: application/json" \
  -d '{"name":"production_test.lua"}'
```
*(Or via HTTP DELETE method: `curl -s -X DELETE "http://espirate-cea0.local/api/script?name=production_test.lua"`)*

---

## 7. REST API Reference & Execution Endpoints

You can execute scripts and control hardware remotely using the `/api/run` endpoint:
* **Synchronous evaluation**: Returns script `print()` output directly in the HTTP response (`"bg": false`).
* **Background worker execution**: Submits job to the non-blocking background worker thread (`"bg": true`).

```bash
# Execute inline Lua snippet synchronously
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code": "<LUA_CODE>", "bg": false}'

# Execute saved LittleFS script in background worker thread
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"file": "/lfs/demo.lua", "bg": true}'
```

---

## 8. Hardware Controller Scripting & Examples

### GPIO Subsystem (`gpio`)

The `gpio` library provides digital I/O sensing, output assertion/deassertion, tristate mode, hardware pull-up/pull-down control, and safety-guarded pin discovery across all accessible ESP32-S3 GPIO pins.

#### Safety Guards & Protected Pins
* **Reserved Flash/PSRAM Pins (26–37):** Forbidden. Modifying these pins raises an error (`"pin X is reserved for Octal SPI Flash/PSRAM bus"`).
* **Reserved USB Pins (19, 20):** Forbidden. These lines carry native USB OTG (`D-`/`D+`) for the CDC-ACM console.
* **Invalid Pins (22–25):** Not bonded on the ESP32-S3 silicon package.
* **Safe, User-Accessible Pins:** 31 pins (`0–18`, `21`, `38–48`).

#### Pin Discovery & Capabilities
```bash
# Query detailed capabilities for GPIO 4
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"local c = gpio.caps(4); print(string.format(\"Pin 4: in=%s out=%s pullup=%s desc=%s\", tostring(c.in), tostring(c.out), tostring(c.pullup), c.desc))", "bg":false}'
```

#### Digital Output Assertion & Deassertion
```bash
# Assert output HIGH (3.3V)
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"gpio.mode(10, \"out\"); gpio.high(10); print(\"Pin 10 asserted HIGH\")", "bg":false}'

# Deassert output LOW (0V)
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"gpio.low(10); print(\"Pin 10 deasserted LOW\")", "bg":false}'

# Toggle pin in a loop
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"gpio.mode(10, \"out\"); for i=1,5 do gpio.toggle(10); sys.sleep(50) end; print(\"Toggled 5 times\")", "bg":false}'
```

#### Digital Input Sensing with Hardware Pull-Up / Pull-Down
```bash
# Configure Pin 4 as input with ~45k internal pull-up and read value
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"gpio.mode(4, \"in\"); gpio.pull(4, \"up\"); print(\"Pin 4 logic level:\", gpio.read(4))", "bg":false}'
```

#### High-Impedance (Tri-State) Mode
```bash
# Put Pin 10 into floating / high-Z state
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"gpio.tristate(10); print(\"Pin 10 set to high-Z\")", "bg":false}'
```

---

### Silicon Matrix Pin Muxing (`matrix`)

The ESP32-S3 internal GPIO Matrix permits peripheral signals (UART, SPI, I2C, PWM) to be routed to **any** physical GPIO pad:

```lua
-- Route internal UART1 TX signal to physical GPIO 18
matrix.route_out(18, matrix.U1TXD)

-- Route physical GPIO 17 into internal UART1 RX signal
gpio.mode(17, "in")
matrix.route_in(17, matrix.U1RXD)

-- Detach matrix routing and restore standard GPIO control
matrix.detach(18)
```

Available Matrix Constants:
* `matrix.U0TXD`, `matrix.U0RXD`
* `matrix.U1TXD`, `matrix.U1RXD`
* `matrix.U2TXD`, `matrix.U2RXD`
* `matrix.I2C0_SCL`, `matrix.I2C0_SDA`
* `matrix.I2C1_SCL`, `matrix.I2C1_SDA`
* `matrix.SPICLK`, `matrix.SPICS0`, `matrix.SPID`, `matrix.SPIQ`

---

### PWM Subsystem (`pwm`)

Hardware PWM control via LEDC for LED dimming, motor speed, and servo control:

```bash
# Start PWM on Pin 12: 5 kHz at 50% duty cycle
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"pwm.open(12, 5000, 50); print(\"PWM active on Pin 12\")", "bg":false}'

# Adjust duty cycle to 80%
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"pwm.duty(12, 80)", "bg":false}'

# Servo motor control (50 Hz period, 7.5% duty = 1.5 ms center pulse)
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"pwm.open(13, 50, 7.5); print(\"Servo centered\")", "bg":false}'

# Stop PWM and release pin
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"pwm.close(12)", "bg":false}'
```

---

### I2C Subsystem (`i2c`)

Communicate with I2C sensors and peripherals:

#### 1. I2C Bus Scanner
```bash
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"local i2c0 = i2c.open(0, 8, 9, 100000); print(\"Scanning I2C bus...\"); for _, addr in ipairs(i2c0:scan()) do print(string.format(\"Found device: 0x%02X\", addr)) end; i2c0:close()", "bg":false}'
```

#### 2. Reading a Sensor Register
```bash
# Read register 0x00 from device address 0x68
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"local bus = i2c.open(0, 8, 9, 400000); local data = bus:read_reg(0x68, 0x00, 1); print(string.format(\"Register 0x00: 0x%02X\", string.byte(data))); bus:close()", "bg":false}'
```

#### 3. Writing to an I2C Device
```bash
# Write 0x00 to register 0x6B on device address 0x68
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"local bus = i2c.open(0, 8, 9, 400000); bus:write_reg(0x68, 0x6B, 0x00); print(\"Device woken up\"); bus:close()", "bg":false}'
```

---

### SPI Subsystem (`spi`)

Master SPI transfers for displays, memory chips, and high-speed sensors:

```bash
# Open SPI2 (SCK=14, MOSI=13, MISO=12, CS=11) at 10 MHz
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"local s = spi.open(2, 14, 13, 12, 11, 10000000, 0); local rx = s:transfer(string.char(0x9F, 0x00, 0x00, 0x00)); print(\"Read JEDEC bytes:\", #rx); s:close()", "bg":false}'
```

---

### UART Subsystem (`uart`)

Auxiliary UART serial communication (e.g. GPS, cellular modems, RS-485):

```bash
# Open UART1 on TX=17, RX=18 at 115200 baud
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code":"local u = uart.open(1, 17, 18, 115200); u:write(\"AT\\r\\n\"); sys.sleep(100); local resp = u:read(64); print(\"Modem reply:\", resp); u:close()", "bg":false}'
```

---

### System & Timing Primitives (`sys`)

```lua
-- Sleep for milliseconds (yields execution to RTOS threads)
sys.sleep(250)

-- Microsecond precision delay (busy-wait)
sys.usleep(50)

-- System uptime in milliseconds
local uptime = sys.uptime()

-- High-resolution 64-bit CPU cycles (240 MHz counter)
local start_cycles = sys.cycles()
gpio.high(10); gpio.low(10)
local elapsed = sys.cycles() - start_cycles
print("Pin toggle CPU cycles:", elapsed)
```

---

### Telemetry & Results Reporting (`telemetry`)

The `telemetry` module provides thread-safe test reporting reflected directly in the Web Dashboard metrics:

```lua
telemetry.reset()
telemetry.set("status", "TESTING")

-- Atomically increment test counters
telemetry.inc("cycles", 1)
telemetry.inc("passed", 1)
telemetry.inc("failed", 0)

telemetry.set("status", "COMPLETE")
telemetry.report()
```

Query telemetry via REST:
```bash
curl -s http://espirate-cea0.local/api/telemetry | jq .
```

Reset telemetry via REST:
```bash
curl -s -X POST http://espirate-cea0.local/api/telemetry/reset
```

---

## 9. Complete Hardware Test Script Example

Here is a complete test sequence (`/lfs/test_pulse.lua`) demonstrating GPIO manipulation, microsecond timing, and real-time telemetry metrics:

```lua
-- ESPirate Hardware Pulse Sequencer Test
print("=== Starting Hardware Pulse Test ===")
telemetry.set("status", "RUNNING")
telemetry.reset()

local PIN = 10
gpio.mode(PIN, "out")

for i = 1, 20 do
  gpio.high(PIN)
  sys.usleep(100) -- 100 µs high pulse
  gpio.low(PIN)
  sys.sleep(50)   -- 50 ms low period

  telemetry.inc("cycles")
  if gpio.read(PIN) == 0 then
    telemetry.inc("passed")
  else
    telemetry.inc("failed")
  end
end

telemetry.set("status", "COMPLETE")
print("=== Pulse Test Complete ===")
telemetry.report()
```
