# ESPirate Lua API Reference Manual

ESPirate integrates a persistent **Lua 5.5** scripting engine into the Zephyr RTOS on the ESP32-S3. This document provides the complete specification and reference for all available Lua hardware, system, and telemetry APIs.

---

## 1. Execution Environments

Lua scripts can be executed via three interfaces:

1. **UART Shell Console:**
   ```bash
   # Single statement or expression
   ESPirate> lua "print('Hello from ESPirate!')"

   # Multi-line statement
   ESPirate> lua "gpio.mode(10, 'out'); for i=1,5 do gpio.toggle(10); sys.sleep(100) end"

   # Execute a script saved in LittleFS non-volatile flash
   ESPirate> lua run /lfs/demo.lua

   # Query Lua VM status & memory consumption
   ESPirate> lua status
   ```

2. **Web Browser Dashboard:**
   * Open `http://espirate-cea0.local` or `http://192.168.0.56` (or `http://192.168.4.1` in AP mode).
   * Enter code in the **Lua Interactive REPL** or select/edit scripts in the **LittleFS Script Manager** and click **Run**.

3. **REST HTTP API:**
   ```bash
   # Run code synchronously
   curl -s -X POST -H "Content-Type: application/json" \
     -d '{"code":"print(\"Uptime: \" .. sys.uptime())","bg":false}' \
     http://espirate-cea0.local/api/run

   # Run a saved LittleFS script in background worker thread
   curl -s -X POST -H "Content-Type: application/json" \
     -d '{"file":"/lfs/demo.lua","bg":true}' \
     http://espirate-cea0.local/api/run
   ```

---

## 2. GPIO Subsystem (`gpio`)

The `gpio` library provides direct digital I/O control across all available ESP32-S3 GPIO pins (GPIO 0 to 48, excluding internal flash/PSRAM pins).

### `gpio.mode(pin, mode)`
Configures the direction and electrical characteristics of a GPIO pin.
* **Parameters:**
  * `pin` *(integer)*: GPIO number (0..48).
  * `mode` *(string)*:
    * `"out"` or `"output"`: Digital output (with input buffer enabled for state readback).
    * `"in"` or `"input"`: Digital input (high impedance, floating).
    * `"in_pullup"` or `"pullup"`: Digital input with internal pull-up resistor (~45 kΩ).
    * `"in_pulldown"` or `"pulldown"`: Digital input with internal pull-down resistor (~45 kΩ).
    * `"open_drain"`: Open-drain output with input buffer enabled.
* **Returns:** `true` on success, or raises a Lua error.
* **Example:**
  ```lua
  gpio.mode(10, "out")
  gpio.mode(11, "in_pullup")
  ```

### `gpio.write(pin, value)`
Sets the logic state of an output pin.
* **Parameters:**
  * `pin` *(integer)*: GPIO number.
  * `value` *(integer)*: `1` for logic HIGH (3.3V), `0` for logic LOW (0V). Non-zero integers evaluate to HIGH.
* **Returns:** `true` on success.
* **Example:**
  ```lua
  gpio.write(10, 1) -- 3.3V
  gpio.write(10, 0) -- 0V
  ```

### `gpio.read(pin)`
Reads the current digital logic level of a pin.
* **Parameters:**
  * `pin` *(integer)*: GPIO number.
* **Returns:** *(integer)* `1` if HIGH, `0` if LOW.
* **Example:**
  ```lua
  local level = gpio.read(11)
  print("Button state:", level)
  ```

### `gpio.toggle(pin)`
Inverts the current output state of a pin (HIGH becomes LOW, LOW becomes HIGH).
* **Parameters:**
  * `pin` *(integer)*: GPIO number.
* **Returns:** `true` on success.
* **Example:**
  ```lua
  gpio.toggle(10)
  ```

### `gpio.high(pin)`
Convenience helper to drive a pin HIGH (equivalent to `gpio.write(pin, 1)`).
* **Parameters:**
  * `pin` *(integer)*: GPIO number.
* **Returns:** `true` on success.

### `gpio.low(pin)`
Convenience helper to drive a pin LOW (equivalent to `gpio.write(pin, 0)`).
* **Parameters:**
  * `pin` *(integer)*: GPIO number.
* **Returns:** `true` on success.

---

## 3. Silicon Matrix Pin Muxing (`matrix`)

The ESP32-S3 features an internal GPIO Matrix that permits almost any internal peripheral signal (UART, SPI, I2C, PWM, timers) to be routed to **any** physical GPIO pad. The `matrix` module provides low-level control over this crossbar switch via ESP-IDF ROM functions.

### `matrix.route_out(pin, signal_idx, [out_inv, oen_inv])`
Connects an internal peripheral output signal to a physical GPIO pad.
* **Parameters:**
  * `pin` *(integer)*: Target GPIO pad (0..48).
  * `signal_idx` *(integer)*: Silicon output signal index (see constants below).
  * `out_inv` *(boolean, optional)*: Invert data output polarity (default: `false`).
  * `oen_inv` *(boolean, optional)*: Invert output enable polarity (default: `false`).
* **Returns:** `true` on success.
* **Example:**
  ```lua
  -- Route UART1 TXD to physical GPIO 18
  matrix.route_out(18, matrix.U1TXD)
  ```

### `matrix.route_in(pin, signal_idx, [inv])`
Connects a physical GPIO pad to an internal peripheral input signal.
* **Parameters:**
  * `pin` *(integer)*: Source GPIO pad (0..48).
  * `signal_idx` *(integer)*: Silicon input signal index (see constants below).
  * `inv` *(boolean, optional)*: Invert input polarity (default: `false`).
* **Returns:** `true` on success.
* **Example:**
  ```lua
  -- Route physical GPIO 19 into UART1 RXD input signal
  gpio.mode(19, "in")
  matrix.route_in(19, matrix.U1RXD)
  ```

### `matrix.detach(pin)`
Disconnects any peripheral signal routed to the pin and restores direct GPIO control.
* **Parameters:**
  * `pin` *(integer)*: GPIO number.
* **Returns:** `true` on success.
* **Example:**
  ```lua
  matrix.detach(18)
  gpio.mode(18, "out")
  gpio.low(18)
  ```

### Pre-defined Hardware Signal Constants

The `matrix` table exports the following signal indices:

| Constant | Description | Direction |
| :--- | :--- | :--- |
| `matrix.SIG_GPIO_OUT` | Default GPIO driver output signal | Output |
| `matrix.U0TXD` | UART0 Transmit Data (default console) | Output |
| `matrix.U0RXD` | UART0 Receive Data | Input |
| `matrix.U1TXD` | UART1 Transmit Data | Output |
| `matrix.U1RXD` | UART1 Receive Data | Input |
| `matrix.U2TXD` | UART2 Transmit Data | Output |
| `matrix.U2RXD` | UART2 Receive Data | Input |
| `matrix.I2C0_SCL` | I2C Controller 0 Clock | Output/Bidirectional |
| `matrix.I2C0_SDA` | I2C Controller 0 Data | Output/Bidirectional |
| `matrix.I2C1_SCL` | I2C Controller 1 Clock | Output/Bidirectional |
| `matrix.I2C1_SDA` | I2C Controller 1 Data | Output/Bidirectional |
| `matrix.SPICLK` | General SPI Clock Output | Output |
| `matrix.SPICS0` | General SPI Chip Select 0 | Output |
| `matrix.SPID` | General SPI MOSI / Data Out | Output |
| `matrix.SPIQ` | General SPI MISO / Data In | Input |
| `matrix.SPIWP` | SPI Write Protect | Bidirectional |
| `matrix.SPIHD` | SPI Hold | Bidirectional |

---

## 4. System & Timing Subsystem (`sys`)

The `sys` module exposes Zephyr RTOS kernel timing primitives for deterministic delays and performance benchmarking.

### `sys.sleep(ms)` / `sys.sleep_ms(ms)` / `sys.msleep(ms)`
Suspends the calling Lua thread for the specified duration in milliseconds, yielding execution time to other Zephyr threads (such as the HTTP server and network stack).
* **Parameters:**
  * `ms` *(integer)*: Milliseconds to sleep.
* **Returns:** None.
* **Example:**
  ```lua
  sys.sleep(500) -- Sleep for 500 ms
  ```

### `sys.usleep(us)`
Executes a precise busy-wait delay in microseconds without yielding thread execution.
* **Parameters:**
  * `us` *(integer)*: Microseconds to busy-wait.
* **Returns:** None.
* **Example:**
  ```lua
  sys.usleep(50) -- 50 microseconds pulse
  ```

### `sys.uptime()`
Returns the system uptime in milliseconds since boot.
* **Parameters:** None.
* **Returns:** *(integer)* Milliseconds since device boot.
* **Example:**
  ```lua
  local start = sys.uptime()
  -- perform test...
  local duration = sys.uptime() - start
  print("Elapsed time:", duration, "ms")
  ```

### `sys.cycles()`
Returns the 64-bit hardware cycle count of the Xtensa LX7 CPU (running at 240 MHz).
* **Parameters:** None.
* **Returns:** *(integer)* 64-bit CPU clock cycles.
* **Example:**
  ```lua
  local c0 = sys.cycles()
  gpio.high(10); gpio.low(10)
  local c1 = sys.cycles()
  print("Pin toggle clock cycles:", c1 - c0)
  ```

---

## 5. Telemetry & Results Reporting (`telemetry`)

The `telemetry` module provides thread-safe communication between running Lua test scripts, the web dashboard, and the UART console.

### `telemetry.set(key, value)`
Sets a named metric or status indicator in the shared telemetry structure.
* **Parameters:**
  * `key` *(string)*: Key name (`"status"`, `"cycles"`, `"passed"`, `"failed"`).
  * `value` *(string or integer)*: Value to store.
* **Returns:** `true` on success.
* **Example:**
  ```lua
  telemetry.set("status", "TESTING")
  telemetry.set("passed", 42)
  ```

### `telemetry.get(key)`
Retrieves a metric value from telemetry.
* **Parameters:**
  * `key` *(string)*: Metric name.
* **Returns:** Value associated with key, or `nil`.

### `telemetry.inc(key, [delta])`
Atomically increments a numerical metric.
* **Parameters:**
  * `key` *(string)*: Metric name (`"cycles"`, `"passed"`, `"failed"`).
  * `delta` *(integer, optional)*: Amount to add (default: `1`).
* **Returns:** `true` on success.
* **Example:**
  ```lua
  telemetry.inc("cycles", 1)
  if passed then
    telemetry.inc("passed", 1)
  else
    telemetry.inc("failed", 1)
  end
  ```

### `telemetry.report()`
Prints the current telemetry metrics directly to the active console/shell.
* **Parameters:** None.
* **Returns:** None.

### `telemetry.reset()`
Resets all telemetry counters and clears status to `"IDLE"`.
* **Parameters:** None.
* **Returns:** None.

---

## 6. Complete Example Script

Here is an example test script (`/lfs/demo.lua`) demonstrating GPIO manipulation, timing, and telemetry:

```lua
-- ESPirate Hardware Test Script
print("=== Starting Hardware Pulse Test ===")
telemetry.set("status", "RUNNING")
telemetry.reset()

local PIN = 10
gpio.mode(PIN, "out")

for i = 1, 10 do
  gpio.high(PIN)
  sys.usleep(100) -- 100 µs pulse
  gpio.low(PIN)
  sys.sleep(50)   -- 50 ms period

  telemetry.inc("cycles")
  if gpio.read(PIN) == 0 then
    telemetry.inc("passed")
  else
    telemetry.inc("failed")
  end
end

telemetry.set("status", "COMPLETE")
print("=== Test Complete ===")
telemetry.report()
```
