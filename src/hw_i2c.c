/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

#include "hw_gpio.h"
#include "hw_i2c.h"

LOG_MODULE_REGISTER(hw_i2c, LOG_LEVEL_INF);

static hw_i2c_status_t s_i2c_state = {
    .scl_pin = -1,
    .sda_pin = -1,
    .speed_khz = 100,
    .active = false,
    .last_scanned_count = 0,
    .last_scanned_addrs = {0}
};

static uint32_t s_half_period_us = 5; /* 100 kHz default */

static void i2c_delay(void)
{
    k_busy_wait(s_half_period_us);
}

static inline void i2c_sda_low(int sda)
{
    hw_gpio_mode(sda, "out", "up");
    hw_gpio_low(sda);
}

static inline void i2c_sda_release(int sda)
{
    hw_gpio_mode(sda, "in", "up");
}

static inline void i2c_scl_low(int scl)
{
    hw_gpio_mode(scl, "out", "up");
    hw_gpio_low(scl);
}

static inline int i2c_scl_release(int scl)
{
    hw_gpio_mode(scl, "in", "up");
    /* Clock stretching support (up to 10 ms) */
    uint32_t start_cycles = k_cycle_get_32();
    uint32_t max_cycles = k_ticks_to_cyc_floor32(k_ms_to_ticks_ceil32(10));
    while (hw_gpio_read(scl) == 0) {
        if ((k_cycle_get_32() - start_cycles) > max_cycles) {
            LOG_WRN("I2C clock stretching timeout on SCL pin %d", scl);
            return -ETIMEDOUT;
        }
        k_busy_wait(1);
    }
    return 0;
}

static void i2c_start(int scl, int sda)
{
    i2c_sda_release(sda);
    i2c_delay();
    i2c_scl_release(scl);
    i2c_delay();
    i2c_sda_low(sda);
    i2c_delay();
    i2c_scl_low(scl);
    i2c_delay();
}

static void i2c_stop(int scl, int sda)
{
    i2c_sda_low(sda);
    i2c_delay();
    i2c_scl_release(scl);
    i2c_delay();
    i2c_sda_release(sda);
    i2c_delay();
}

static int i2c_write_byte(int scl, int sda, uint8_t byte)
{
    for (int i = 7; i >= 0; i--) {
        if ((byte >> i) & 1) {
            i2c_sda_release(sda);
        } else {
            i2c_sda_low(sda);
        }
        i2c_delay();
        i2c_scl_release(scl);
        i2c_delay();
        i2c_scl_low(scl);
    }

    /* Release SDA to sample ACK/NACK */
    i2c_sda_release(sda);
    i2c_delay();
    i2c_scl_release(scl);
    i2c_delay();
    int ack = hw_gpio_read(sda);
    i2c_scl_low(scl);
    i2c_delay();

    return (ack == 0) ? 0 : -EIO;
}

static uint8_t i2c_read_byte(int scl, int sda, bool ack)
{
    uint8_t byte = 0;
    i2c_sda_release(sda);

    for (int i = 7; i >= 0; i--) {
        i2c_delay();
        i2c_scl_release(scl);
        if (hw_gpio_read(sda)) {
            byte |= (1U << i);
        }
        i2c_delay();
        i2c_scl_low(scl);
    }

    /* Send ACK (0) or NACK (1) */
    if (ack) {
        i2c_sda_low(sda);
    } else {
        i2c_sda_release(sda);
    }
    i2c_delay();
    i2c_scl_release(scl);
    i2c_delay();
    i2c_scl_low(scl);
    i2c_sda_release(sda);
    i2c_delay();

    return byte;
}

static void i2c_recover_bus(int scl, int sda)
{
    i2c_sda_release(sda);
    for (int i = 0; i < 9; i++) {
        i2c_scl_low(scl);
        i2c_delay();
        i2c_scl_release(scl);
        i2c_delay();
    }
    i2c_stop(scl, sda);
}

int hw_i2c_init_bus(int scl_pin, int sda_pin, uint32_t speed_khz)
{
    const char *reason = NULL;
    int ret = hw_gpio_check_safety(scl_pin, &reason);
    if (ret != 0) return ret;
    ret = hw_gpio_check_safety(sda_pin, &reason);
    if (ret != 0) return ret;

    if (scl_pin == sda_pin) return -EINVAL;

    if (speed_khz == 0 || speed_khz > 400) {
        speed_khz = 100;
    }

    s_half_period_us = 500 / speed_khz;
    if (s_half_period_us < 2) s_half_period_us = 2;

    i2c_recover_bus(scl_pin, sda_pin);

    s_i2c_state.scl_pin = scl_pin;
    s_i2c_state.sda_pin = sda_pin;
    s_i2c_state.speed_khz = speed_khz;
    s_i2c_state.active = true;

    LOG_INF("I2C bus configured: SCL=%d, SDA=%d at %u kHz",
            scl_pin, sda_pin, speed_khz);
    return 0;
}

int hw_i2c_scan(int scl_pin, int sda_pin, uint8_t *found_addrs, size_t max_addrs, size_t *count)
{
    int ret = hw_i2c_init_bus(scl_pin, sda_pin, s_i2c_state.speed_khz ? s_i2c_state.speed_khz : 100);
    if (ret != 0) return ret;

    size_t found = 0;

    /* Scan standard 7-bit addresses 0x08 to 0x77 */
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        i2c_start(scl_pin, sda_pin);
        int ack = i2c_write_byte(scl_pin, sda_pin, (addr << 1));
        i2c_stop(scl_pin, sda_pin);

        if (ack == 0) {
            if (found_addrs && found < max_addrs) {
                found_addrs[found] = addr;
            }
            if (found < sizeof(s_i2c_state.last_scanned_addrs)) {
                s_i2c_state.last_scanned_addrs[found] = addr;
            }
            found++;
        }
    }

    s_i2c_state.last_scanned_count = (uint8_t)found;
    if (count) *count = found;

    return 0;
}

int hw_i2c_write(int scl_pin, int sda_pin, uint8_t addr, const uint8_t *buf, size_t len)
{
    if (!buf && len > 0) return -EINVAL;

    int ret = hw_i2c_init_bus(scl_pin, sda_pin, s_i2c_state.speed_khz ? s_i2c_state.speed_khz : 100);
    if (ret != 0) return ret;

    i2c_start(scl_pin, sda_pin);
    ret = i2c_write_byte(scl_pin, sda_pin, (addr << 1));
    if (ret != 0) {
        i2c_stop(scl_pin, sda_pin);
        return -ENODEV; /* Address NACKed */
    }

    for (size_t i = 0; i < len; i++) {
        ret = i2c_write_byte(scl_pin, sda_pin, buf[i]);
        if (ret != 0) {
            i2c_stop(scl_pin, sda_pin);
            return -EIO;
        }
    }

    i2c_stop(scl_pin, sda_pin);
    return 0;
}

int hw_i2c_read(int scl_pin, int sda_pin, uint8_t addr, uint8_t *buf, size_t len)
{
    if (!buf || len == 0) return -EINVAL;

    int ret = hw_i2c_init_bus(scl_pin, sda_pin, s_i2c_state.speed_khz ? s_i2c_state.speed_khz : 100);
    if (ret != 0) return ret;

    i2c_start(scl_pin, sda_pin);
    ret = i2c_write_byte(scl_pin, sda_pin, (addr << 1) | 1);
    if (ret != 0) {
        i2c_stop(scl_pin, sda_pin);
        return -ENODEV;
    }

    for (size_t i = 0; i < len; i++) {
        bool send_ack = (i + 1 < len);
        buf[i] = i2c_read_byte(scl_pin, sda_pin, send_ack);
    }

    i2c_stop(scl_pin, sda_pin);
    return 0;
}

int hw_i2c_write_read(int scl_pin, int sda_pin, uint8_t addr,
                      const uint8_t *tx_buf, size_t tx_len,
                      uint8_t *rx_buf, size_t rx_len)
{
    if (!rx_buf || rx_len == 0) return -EINVAL;

    int ret = hw_i2c_init_bus(scl_pin, sda_pin, s_i2c_state.speed_khz ? s_i2c_state.speed_khz : 100);
    if (ret != 0) return ret;

    i2c_start(scl_pin, sda_pin);
    ret = i2c_write_byte(scl_pin, sda_pin, (addr << 1));
    if (ret != 0) {
        i2c_stop(scl_pin, sda_pin);
        return -ENODEV;
    }

    for (size_t i = 0; i < tx_len; i++) {
        ret = i2c_write_byte(scl_pin, sda_pin, tx_buf[i]);
        if (ret != 0) {
            i2c_stop(scl_pin, sda_pin);
            return -EIO;
        }
    }

    /* Repeated START */
    i2c_start(scl_pin, sda_pin);
    ret = i2c_write_byte(scl_pin, sda_pin, (addr << 1) | 1);
    if (ret != 0) {
        i2c_stop(scl_pin, sda_pin);
        return -ENODEV;
    }

    for (size_t i = 0; i < rx_len; i++) {
        bool send_ack = (i + 1 < rx_len);
        rx_buf[i] = i2c_read_byte(scl_pin, sda_pin, send_ack);
    }

    i2c_stop(scl_pin, sda_pin);
    return 0;
}

int hw_i2c_get_status(hw_i2c_status_t *status)
{
    if (!status) return -EINVAL;
    *status = s_i2c_state;
    return 0;
}
