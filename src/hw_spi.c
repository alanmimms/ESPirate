/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

#include "hw_gpio.h"
#include "hw_spi.h"

LOG_MODULE_REGISTER(hw_spi, LOG_LEVEL_INF);

static hw_spi_status_t s_spi_state = {
    .sck_pin = -1,
    .mosi_pin = -1,
    .miso_pin = -1,
    .cs_pin = -1,
    .freq_khz = 1000,
    .mode = 0,
    .active = false
};

static uint32_t s_half_period_us = 1;

static void spi_delay(void)
{
    if (s_half_period_us > 0) {
        k_busy_wait(s_half_period_us);
    }
}

int hw_spi_init_bus(int sck_pin, int mosi_pin, int miso_pin, int cs_pin,
                    uint32_t freq_khz, uint8_t mode)
{
    const char *reason = NULL;
    int ret;

    ret = hw_gpio_check_safety(sck_pin, &reason);
    if (ret != 0) return ret;

    if (mosi_pin >= 0) {
        ret = hw_gpio_check_safety(mosi_pin, &reason);
        if (ret != 0) return ret;
    }
    if (miso_pin >= 0) {
        ret = hw_gpio_check_safety(miso_pin, &reason);
        if (ret != 0) return ret;
    }
    if (cs_pin >= 0) {
        ret = hw_gpio_check_safety(cs_pin, &reason);
        if (ret != 0) return ret;
    }

    if (mode > 3) mode = 0;
    bool cpol = (mode == 2 || mode == 3);

    /* Configure SCK */
    hw_gpio_mode(sck_pin, "out", NULL);
    hw_gpio_write(sck_pin, cpol ? 1 : 0);

    /* Configure MOSI */
    if (mosi_pin >= 0) {
        hw_gpio_mode(mosi_pin, "out", NULL);
        hw_gpio_write(mosi_pin, 0);
    }

    /* Configure MISO with pullup */
    if (miso_pin >= 0) {
        hw_gpio_mode(miso_pin, "in", "pullup");
    }

    /* Configure CS (idle high) */
    if (cs_pin >= 0) {
        hw_gpio_mode(cs_pin, "out", NULL);
        hw_gpio_write(cs_pin, 1);
    }

    if (freq_khz == 0) freq_khz = 1000;
    s_half_period_us = 500 / freq_khz;
    if (s_half_period_us == 0) s_half_period_us = 1;

    s_spi_state.sck_pin = sck_pin;
    s_spi_state.mosi_pin = mosi_pin;
    s_spi_state.miso_pin = miso_pin;
    s_spi_state.cs_pin = cs_pin;
    s_spi_state.freq_khz = freq_khz;
    s_spi_state.mode = mode;
    s_spi_state.active = true;

    LOG_INF("SPI bus configured: SCK=%d, MOSI=%d, MISO=%d, CS=%d (Mode %u, %u kHz)",
            sck_pin, mosi_pin, miso_pin, cs_pin, mode, freq_khz);
    return 0;
}

static uint8_t spi_transfer_byte(int sck, int mosi, int miso, uint8_t mode, uint8_t out_byte)
{
    uint8_t in_byte = 0;
    bool cpol = (mode == 2 || mode == 3);
    bool cpha = (mode == 1 || mode == 3);

    for (int i = 7; i >= 0; i--) {
        uint8_t bit = (out_byte >> i) & 1;

        if (!cpha) {
            /* CPHA=0: Data is driven before leading edge, sampled on leading edge */
            if (mosi >= 0) hw_gpio_write(mosi, bit);
            spi_delay();
            hw_gpio_write(sck, cpol ? 0 : 1); /* Leading edge */
            spi_delay();
            if (miso >= 0 && hw_gpio_read(miso)) in_byte |= (1U << i);
            hw_gpio_write(sck, cpol ? 1 : 0); /* Trailing edge */
        } else {
            /* CPHA=1: Leading edge occurs, data is driven, sampled on trailing edge */
            hw_gpio_write(sck, cpol ? 0 : 1); /* Leading edge */
            if (mosi >= 0) hw_gpio_write(mosi, bit);
            spi_delay();
            hw_gpio_write(sck, cpol ? 1 : 0); /* Trailing edge */
            spi_delay();
            if (miso >= 0 && hw_gpio_read(miso)) in_byte |= (1U << i);
        }
    }
    return in_byte;
}

int hw_spi_transfer(int sck_pin, int mosi_pin, int miso_pin, int cs_pin,
                    uint8_t mode, const uint8_t *tx, uint8_t *rx, size_t len)
{
    int ret = hw_spi_init_bus(sck_pin, mosi_pin, miso_pin, cs_pin, s_spi_state.freq_khz, mode);
    if (ret != 0) return ret;

    /* Assert CS */
    if (cs_pin >= 0) {
        hw_gpio_write(cs_pin, 0);
        spi_delay();
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t tx_b = tx ? tx[i] : 0xFF;
        uint8_t rx_b = spi_transfer_byte(sck_pin, mosi_pin, miso_pin, mode, tx_b);
        if (rx) {
            rx[i] = rx_b;
        }
    }

    /* Deassert CS */
    if (cs_pin >= 0) {
        spi_delay();
        hw_gpio_write(cs_pin, 1);
    }

    return 0;
}

int hw_spi_write(int sck_pin, int mosi_pin, int cs_pin, uint8_t mode,
                 const uint8_t *tx, size_t len)
{
    return hw_spi_transfer(sck_pin, mosi_pin, -1, cs_pin, mode, tx, NULL, len);
}

int hw_spi_read(int sck_pin, int miso_pin, int cs_pin, uint8_t mode,
                uint8_t *rx, size_t len)
{
    return hw_spi_transfer(sck_pin, -1, miso_pin, cs_pin, mode, NULL, rx, len);
}

int hw_spi_get_status(hw_spi_status_t *status)
{
    if (!status) return -EINVAL;
    *status = s_spi_state;
    return 0;
}
