# ESPirate: Software-Defined Hardware Sequencing Architecture

The ESPirate system provides a standalone, Lua-driven hardware testing
and sequencing platform built on the Zephyr RTOS and targeting the
ESP32-S3 microcontroller. It functions as a modern, untethered
replacement for traditional tools like the Bus Pirate, executing test
scripts, managing dynamic pinouts, and reporting telemetry entirely
on-device without requiring an active host PC.

## 1. Dynamic Hardware Muxing via Lua

Zephyr natively relies on a static, compile-time Device Tree (DTS),
which is incompatible with a runtime, software-defined pinout. To
solve this, ESPirate virtualizes the hardware abstraction layer by
leveraging the ESP32-S3's highly flexible GPIO matrix alongside
Zephyr’s driver models.

* **Pre-allocated Controllers:** All primary peripherals (UART, SPI,
  I2C, PWM) are statically defined in the Zephyr DTS and mapped to
  safe, internal dummy pins during boot.
* **Lua Hardware Manager:** A C-to-Lua binding layer exposes an API
  allowing the Lua script to dynamically request specific peripheral
  signals (e.g., `i2c0Sda`, `uart1Tx`) and assign them to physical
  GPIO pads.
* **Direct ESP-IDF Matrix Routing:** When the Lua API is invoked, the
  underlying C functions bypass Zephyr's static pin control and use
  ESP-IDF ROM functions (`esp_rom_gpio_connect_out_signal`,
  `esp_rom_gpio_connect_in_signal`) to route the pre-allocated Zephyr
  device handles to the user-requested physical pins.
* **Zephyr Abstraction Integrity:** The physical routing is handled at
  the silicon matrix level, allowing Zephyr to continue managing
  interrupts, DMA, and high-level peripheral APIs seamlessly.

## 2. Storage and Connectivity

The system is designed for untethered field use, storing its own
executable tests and providing its own interfaces for management and
telemetry.

* **LittleFS Partition:** The ESP32-S3 flash contains a dedicated
  LittleFS partition mounted via Zephyr’s POSIX API. Lua's standard
  `io` library reads and executes `.lua` scripts directly from this
  non-volatile storage, allowing sequences to survive power cycles.
* **Wi-Fi Access Point & Web Server:** Zephyr’s networking stack hosts
  a standalone Wi-Fi AP and an embedded HTTP server. This provides a
  REST API to upload new Lua scripts to LittleFS, trigger test
  executions, and poll for pass/fail telemetry via a simple web
  interface.
* **USB CDC ACM (Native Serial):** For tethered use, the ESP32-S3’s
  native USB OTG controller is configured as a serial device. This
  routes standard UART API calls over USB, providing a
  machine-readable control channel for host scripts or a command-line
  interface via Zephyr’s Shell subsystem.

## 3. Concurrency and IPC Model

Because hardware sequencing requires blocking delays and continuous
execution loops, the system isolates the Lua runtime from
communication interfaces using Zephyr's threading and Inter-Process
Communication (IPC) primitives.

* **Isolated Threading:** The architecture relies on three primary
  threads: a high-priority Web Server thread, a USB/Shell thread, and
  a dedicated Lua Worker thread. This ensures the UI and serial
  interfaces remain highly responsive while Lua executes blocking test
  loops.
* **Execution Triggering:** Both the Web Server and USB interfaces can
  initiate tests by writing a target filename to a shared Zephyr
  message queue (`k_msgq`). The Lua Worker thread pends on this queue,
  executing scripts as requested regardless of the originating
  interface.
* **Telemetry Synchronization:** As the Lua script cycles through
  tests, it invokes a C-bound telemetry API. This API safely updates a
  global status struct (tracking cycle counts and pass/fail metrics)
  protected by a Zephyr mutex (`k_mutex`).
* **Live Reporting:** The Web Server thread safely locks the mutex to
  serve JSON status to browser polling requests, while the USB thread
  can directly stream formatted telemetry strings to the serial
  console.
