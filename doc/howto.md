# ESPirate How-To Guide & API Reference

ESPirate integrates a persistent **Lua 5.5** scripting engine, an active web dashboard, a rich UART shell, and unified hardware peripheral drivers into the Zephyr RTOS on the **ESP32-S3**. This document is the comprehensive reference for building, flashing, monitoring, managing flash storage, transferring files, and controlling hardware peripherals (GPIO, Matrix, PWM, I2C, SPI, System, Telemetry).

---

## Architecture & Unified Foundation

All three control surfaces—**UART Shell Commands**, **Lua Scripting Engine**, and **HTTP REST API**—route directly through the same underlying C Hardware Abstraction Layer (`hw_gpio`, `hw_pwm`, `hw_i2c`, `hw_spi`). There is **zero code duplication**:
* Pin safety rules are enforced uniformly across all interfaces.
* Hardware states and active channels are shared and synchronized.
* An action performed in the UART shell or via REST is immediately visible in Lua scripts and vice versa.

---

## Table of Contents
1. [Environment Setup](#1-environment-setup)
2. [Building Firmware](#2-building-firmware)
3. [Flashing & Hardware Erase](#3-flashing--hardware-erase)
4. [Serial Console Monitoring](#4-serial-console-monitoring)
5. [LittleFS Storage Management & Erasing](#5-littlefs-storage-management--erasing)
6. [Copying Files To/From LittleFS with cURL](#6-copying-files-tofrom-littlefs-with-curl)
7. [REST API Reference & Execution Endpoints](#7-rest-api-reference--execution-endpoints)
8. [Hardware Controller Unified Reference](#8-hardware-controller-unified-reference)
   - [GPIO Subsystem (`gpio`)](#gpio-subsystem-gpio)
   - [Silicon Matrix Pin Muxing (`matrix`)](#silicon-matrix-pin-muxing-matrix)
   - [PWM Subsystem (`pwm`)](#pwm-subsystem-pwm)
   - [I2C Master Subsystem (`i2c`)](#i2c-master-subsystem-i2c)
   - [SPI Master Subsystem (`spi`)](#spi-master-subsystem-spi)
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

### Web Dashboard UI Development (`web/index.html`)

The ESPirate web dashboard frontend is maintained in [`web/index.html`](file:///home/alan/ha/ESPirate/web/index.html) as clean, standard HTML5/CSS/JavaScript.

* **Automatic Generation**: CMake automatically converts `web/index.html` into a C header at build time using [`scripts/generate_web_dashboard.py`](file:///home/alan/ha/ESPirate/scripts/generate_web_dashboard.py).
* **Incremental Recompilation**: Whenever `web/index.html` is edited, running `./scripts/build.sh` automatically regenerates `build/include/web_dashboard.h`, forces recompilation of `src/web_server.c`, and relinks the binary.
* **Direct Script Execution**: You can also run the generator manually:
  ```bash
  python3 scripts/generate_web_dashboard.py web/index.html build/include/web_dashboard.h
  ```

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
ESPirate> gpio status
ESPirate> pwm status
ESPirate> i2c status
ESPirate> spi status
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
  test_pulse.lua                 2481
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
   curl -s -X POST http://espirate-cea0.local/api/storage/format
   ```
3. **Via Web Dashboard:**
   Click the **Format** button in the Storage panel.
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
  { "name": "test_pulse.lua", "size": 2481 }
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

#### Using Python (no `jq` required):
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

### Remote Script Execution (`/api/run`)
* **Synchronous evaluation**: Returns script `print()` output directly in the HTTP response (`"bg": false`).
* **Background worker execution**: Submits job to the non-blocking background worker thread (`"bg": true`).

```bash
# Execute inline Lua snippet synchronously
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"code": "print(gpio.read(4))", "bg": false}'

# Execute saved LittleFS script in background worker thread
curl -s -X POST http://espirate-cea0.local/api/run \
  -H "Content-Type: application/json" \
  -d '{"file": "/lfs/demo.lua", "bg": true}'
```

### System & Telemetry Endpoints
* `GET /api/status`: Subsystem status, heap usage, Wi-Fi connectivity, LittleFS mount.
* `GET /api/telemetry`: Hardware test metrics (total, passed, failed cycles).
* `POST /api/telemetry/reset`: Reset test counters.
* `POST /api/reset`: Reset Lua VM state.

---

## 8. Hardware Controller Unified Reference

Every peripheral can be commanded via the **UART Shell**, scripted in **Lua**, or controlled remotely via the **REST API**.

---

### GPIO Subsystem (`gpio`)

Digital I/O pin configuration, direct level assertion/deassertion, logic sensing, pull resistors, drive strength, and safety checks.

#### Pin Safety Model
* **Reserved Flash/PSRAM Pins (26–37):** Strictly forbidden.
* **Reserved USB Console Pins (19, 20):** Forbidden (native USB CDC-ACM console).
* **Unbonded Silicon Pins (22–25):** Not bonded on ESP32-S3 package.
* **Safe, User-Accessible Pins (31 pads):** `0..18`, `21`, `38..48`.

#### 1. UART Shell Commands
```bash
# Query full GPIO state table
ESPirate> gpio status
=== ESP32-S3 GPIO State Table ===
Pin  State & Configuration               Level  Notes
---  ----------------------------------  -----  ---------------------------
 0   TRISTATE [FLOATING]                   1    Strapping Pin (BOOT button)
 4   TRISTATE [FLOATING]                   0    General Purpose I/O
...

# Query single pin state
ESPirate> gpio status 4
GPIO  4 : TRISTATE [FLOATING]              | Level: 0

# Show hardware capabilities and ADC channel mapping
ESPirate> gpio info 4
=== GPIO Pin 4 Metadata ===
  Bonded Silicon Pad : YES
  System Reserved    : NO (Safe)
  Digital Input      : SUPPORTED
  Digital Output     : SUPPORTED
  Pull-up/Pull-down  : SUPPORTED
  Analog ADC Input   : ADC1_CH3
  Description        : General Purpose I/O

# Configure pin direction and pull
ESPirate> gpio mode 4 out
ESPirate> gpio mode 4 in up

# Write digital outputs
ESPirate> gpio high 4
GPIO 4 set to 1 (HIGH)

ESPirate> gpio low 4
GPIO 4 set to 0 (LOW)

ESPirate> gpio toggle 4
GPIO 4 toggled -> 1 (HIGH)

# Read digital input
ESPirate> gpio read 4
GPIO 4 = 1 (HIGH)

# Float / disconnect pin (Hi-Z)
ESPirate> gpio tristate 4
GPIO 4 set to High-Impedance / Tristate (Hi-Z)

# Set drive strength (5, 10, 20, or 40 mA)
ESPirate> gpio drive 4 20
GPIO 4 drive strength set to 20 mA
```

#### 2. Lua API
```lua
-- Configure pin mode: "in", "out", "open_drain", "tristate" with optional "up", "down", "none"
gpio.mode(4, "out")
gpio.mode(5, "in", "up")

-- Read & Write
gpio.write(4, 1)          -- or gpio.high(4)
gpio.write(4, 0)          -- or gpio.low(4)
local val = gpio.read(5)  -- returns 0 or 1
gpio.toggle(4)

-- Tristate (Hi-Z)
gpio.tristate(4)

-- Query status table
local st = gpio.status(4)
print(st.pin, st.mode, st.level)

-- Query pin capabilities
local caps = gpio.info(4)
print("ADC:", caps.adc, "Analog:", caps.analog)
```

#### 3. REST API
```bash
# Read pin 4 state
curl -s "http://espirate-cea0.local/api/gpio?pin=4" | jq .
# Response: {"pin":4, "mode":"OUT (PUSH_PULL) [FLOATING]", "level":1}

# Set pin 4 HIGH
curl -s -X POST http://espirate-cea0.local/api/gpio \
  -H "Content-Type: application/json" \
  -d '{"pin": 4, "action": "high"}'

# Configure mode and write value
curl -s -X POST http://espirate-cea0.local/api/gpio \
  -H "Content-Type: application/json" \
  -d '{"pin": 4, "mode": "out", "value": 1}'

# Tristate pin 4
curl -s -X POST http://espirate-cea0.local/api/gpio \
  -H "Content-Type: application/json" \
  -d '{"pin": 4, "action": "tristate"}'
```

---

### Silicon Matrix Pin Muxing (`matrix`)

Routes internal peripheral output signals to arbitrary physical pads, or routes arbitrary pads into internal peripheral inputs.

#### 1. UART Shell Commands
```bash
# Show matrix routes
ESPirate> matrix status
=== GPIO Matrix Routing Table ===
(No peripheral signals currently routed via GPIO matrix)

# List common peripheral signal IDs
ESPirate> matrix list
=== Common ESP32-S3 Output Signals ===
Signal ID  Name / Peripheral
---------  ----------------------------------------
 256       GPIO_OUT (Default GPIO output)
  12       U0TXD (UART0 Console TX)
  15       U1TXD (UART1 TX)
  18       U2TXD (UART2 TX)
  89       I2C0_SCL (I2C0 Clock)
  90       I2C0_SDA (I2C0 Data)
  91       I2C1_SCL (I2C1 Clock)
  92       I2C1_SDA (I2C1 Data)
 101       SPICLK (SPI2 Clock)
 103       SPID (SPI2 MOSI)
 110       SPICS0 (SPI2 CS0)
  73.. 80  LEDC_OUT0..7 (LEDC Hardware PWM Channels 0..7)

# Route UART1 TX (signal 15) to GPIO 18
ESPirate> matrix route_out 18 15
Routed Signal 15 (U1TXD (UART1 TX)) to GPIO 18

# Detach pin from matrix back to default GPIO
ESPirate> matrix detach 18
GPIO 18 detached from matrix (restored to standard GPIO)
```

#### 2. Lua API
```lua
-- Route internal UART1 TX to physical GPIO 18
matrix.route_out(18, matrix.U1TXD)

-- Route physical GPIO 17 into internal UART1 RX
gpio.mode(17, "in")
matrix.route_in(17, matrix.U1RXD)

-- Detach matrix routing
matrix.detach(18)
```

---

### PWM Subsystem (`pwm`)

ESP32-S3 LEDC 8-channel hardware PWM controller. Supports frequencies from 5 Hz to 40 MHz with automatic resolution and clock divider selection. Can be routed to any safe GPIO pad.

#### 1. UART Shell Commands
```bash
# Start PWM on GPIO 10 at 1 kHz, 50% duty cycle
ESPirate> pwm set 10 1000 50
PWM active on GPIO 10: 1000 Hz, 50% duty cycle

# Check active PWM channels
ESPirate> pwm status
=== LEDC Hardware PWM Channels (1 Active) ===
Channel  GPIO Pin  Frequency (Hz)  Duty Cycle (%)  Active
-------  --------  --------------  --------------  ------
    0       10           1000 Hz          50%        YES

# Stop PWM and release channel
ESPirate> pwm stop 10
PWM stopped on GPIO 10 (channel released, pin tristated)
```

#### 2. Lua API
```lua
-- Start PWM: pin, freq_hz, duty_percent
pwm.set(10, 1000, 50)

-- Check status of specific pin
local st = pwm.status(10)
print("PWM Channel:", st.channel, "Freq:", st.freq, "Duty:", st.duty)

-- Stop PWM
pwm.stop(10)
```

#### 3. REST API
```bash
# Query active PWM channels
curl -s http://espirate-cea0.local/api/pwm | jq .
# Response: [{"channel":0, "pin":10, "freq_hz":1000, "duty_percent":50}]

# Set PWM on pin 10
curl -s -X POST http://espirate-cea0.local/api/pwm \
  -H "Content-Type: application/json" \
  -d '{"pin": 10, "freq": 1000, "duty": 50}'

# Stop PWM on pin 10
curl -s -X POST http://espirate-cea0.local/api/pwm \
  -H "Content-Type: application/json" \
  -d '{"action": "stop", "pin": 10}'
```

---

### I2C Master Subsystem (`i2c`)

I2C master supporting arbitrary safe SCL/SDA pins, 7-bit bus scanning (0x08..0x77), multi-byte read/write, repeated-start write-read, and clock stretching.

#### 1. UART Shell Commands
```bash
# Scan I2C bus on SCL=5, SDA=4
ESPirate> i2c scan 5 4
Scanning 7-bit I2C bus (SCL=5, SDA=4)...
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- 3c -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- --                         
Scan complete: 1 device(s) found.

# Query bus status
ESPirate> i2c status
=== I2C Subsystem Status ===
  Bus Active : YES
  SCL Pin    : 5
  SDA Pin    : 4
  Speed      : 100 kHz
  Last Scan  : 1 device(s) found
  Addresses  : 0x3C

# Write bytes to device (address 0x3C)
ESPirate> i2c write 5 4 0x3C 0x00 0xAF
Wrote 2 bytes to I2C device 0x3C

# Read 4 bytes from device (address 0x68)
ESPirate> i2c read 5 4 0x68 4
Read 4 bytes from 0x68:
00 01 02 03

# Write register pointer then read response (repeated-start)
ESPirate> i2c write_read 5 4 0x68 0x75 1
Received 1 bytes from 0x68:
68
```

#### 2. Lua API
```lua
-- Bus scan: returns array of detected 7-bit addresses
local devices = i2c.scan(5, 4)
for _, addr in ipairs(devices) do
  print(string.format("Found device: 0x%02X", addr))
end

-- Write bytes: accepts varargs, array table, or string
i2c.write(5, 4, 0x3C, 0x00, 0xAF)
i2c.write(5, 4, 0x3C, {0x00, 0xAF})

-- Read bytes: returns array of byte integers
local data = i2c.read(5, 4, 0x68, 2)
print("Byte 0:", data[1], "Byte 1:", data[2])

-- Write-read (repeated start for sensor register reading)
local reg_val = i2c.write_read(5, 4, 0x68, 0x75, 1)
print(string.format("WHO_AM_I: 0x%02X", reg_val[1]))
```

#### 3. REST API
```bash
# Query I2C bus status
curl -s http://espirate-cea0.local/api/i2c | jq .

# Perform bus scan
curl -s -X POST http://espirate-cea0.local/api/i2c/scan \
  -H "Content-Type: application/json" \
  -d '{"scl": 5, "sda": 4}' | jq .
# Response: {"status":"ok", "found":["0x3C"]}

# Write bytes to I2C device
curl -s -X POST http://espirate-cea0.local/api/i2c/write \
  -H "Content-Type: application/json" \
  -d '{"scl": 5, "sda": 4, "addr": 60, "data": [0, 175]}'

# Read bytes from I2C device
curl -s -X POST http://espirate-cea0.local/api/i2c/read \
  -H "Content-Type: application/json" \
  -d '{"scl": 5, "sda": 4, "addr": 60, "len": 2}' | jq .
# Response: {"status":"ok", "data":[0, 175]}
```

---

### SPI Master Subsystem (`spi`)

Full-duplex SPI master controller supporting arbitrary safe SCK/MOSI/MISO/CS pins, SPI modes 0..3, and configurable clock speeds.

#### 1. UART Shell Commands
```bash
# Full duplex transfer (SCK=12, MOSI=11, MISO=13, CS=10, Mode=0)
ESPirate> spi transfer 12 11 13 10 0 0x9F 0x00 0x00 0x00
SPI Transfer Result (4 bytes):
  TX: 9F 00 00 00 
  RX: EF 40 18 00 

# Check SPI bus status
ESPirate> spi status
=== SPI Subsystem Status ===
  Bus Active : YES
  SCK Pin    : 12
  MOSI Pin   : 11
  MISO Pin   : 13
  CS Pin     : 10
  Frequency  : 1000 kHz
  Mode       : 0 (CPOL=0, CPHA=0)

# Write bytes only
ESPirate> spi write 12 11 10 0 0x06
Transmitted 1 bytes over SPI

# Read bytes only
ESPirate> spi read 12 13 10 0 4
Read 4 bytes from SPI:
EF 40 18 00
```

#### 2. Lua API
```lua
-- Full duplex transfer: returns array of received byte integers
local rx = spi.transfer(12, 11, 13, 10, 0, 0x9F, 0x00, 0x00, 0x00)
print(string.format("JEDEC ID: 0x%02X 0x%02X 0x%02X", rx[2], rx[3], rx[4]))

-- Write only
spi.write(12, 11, 10, 0, {0x06})

-- Read only
local data = spi.read(12, 13, 10, 0, 4)
```

#### 3. REST API
```bash
# Query SPI status
curl -s http://espirate-cea0.local/api/spi | jq .

# Perform SPI transfer
curl -s -X POST http://espirate-cea0.local/api/spi/transfer \
  -H "Content-Type: application/json" \
  -d '{"sck":12, "mosi":11, "miso":13, "cs":10, "mode":0, "data":[159, 0, 0, 0]}' | jq .
# Response: {"status":"ok", "rx":[239, 64, 24, 0]}
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

Here is a complete test sequence (`/lfs/test_pulse.lua`) demonstrating GPIO manipulation, PWM activation, microsecond timing, and real-time telemetry metrics:

```lua
-- ESPirate Hardware Sequencer & Verification Script
print("=== Starting ESPirate Hardware Test ===")
telemetry.reset()
telemetry.set("status", "RUNNING")

local TEST_PIN = 4
local PWM_PIN  = 10

-- 1. Setup GPIO and PWM
gpio.mode(TEST_PIN, "out")
pwm.set(PWM_PIN, 5000, 50) -- 5 kHz, 50% duty

for i = 1, 10 do
  gpio.high(TEST_PIN)
  sys.usleep(500) -- 500 µs high pulse
  gpio.low(TEST_PIN)
  sys.sleep(50)   -- 50 ms low period

  telemetry.inc("cycles")
  if gpio.read(TEST_PIN) == 0 then
    telemetry.inc("passed")
  else
    telemetry.inc("failed")
  end
end

-- 2. Stop PWM and tristate test pin
pwm.stop(PWM_PIN)
gpio.tristate(TEST_PIN)

telemetry.set("status", "COMPLETE")
print("=== Hardware Test Complete ===")
```
