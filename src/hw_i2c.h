/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_HW_I2C_H_
#define ESPIRATE_HW_I2C_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int scl_pin;
    int sda_pin;
    uint32_t speed_khz;
    bool active;
    uint8_t last_scanned_count;
    uint8_t last_scanned_addrs[128];
} hw_i2c_status_t;

/**
 * @brief Initialize/Configure the I2C bus pins and speed.
 */
int hw_i2c_init_bus(int scl_pin, int sda_pin, uint32_t speed_khz);

/**
 * @brief Perform a 7-bit address bus scan (0x08..0x77).
 * @param scl_pin SCL GPIO pin.
 * @param sda_pin SDA GPIO pin.
 * @param found_addrs Buffer to receive detected 7-bit addresses.
 * @param max_addrs Max entries in found_addrs.
 * @param count Output pointer to receive total detected count.
 * @return 0 on success, negative errno on failure.
 */
int hw_i2c_scan(int scl_pin, int sda_pin, uint8_t *found_addrs, size_t max_addrs, size_t *count);

/**
 * @brief Write bytes to an I2C device address.
 */
int hw_i2c_write(int scl_pin, int sda_pin, uint8_t addr, const uint8_t *buf, size_t len);

/**
 * @brief Read bytes from an I2C device address.
 */
int hw_i2c_read(int scl_pin, int sda_pin, uint8_t addr, uint8_t *buf, size_t len);

/**
 * @brief Write register/command bytes then read back response (repeated START).
 */
int hw_i2c_write_read(int scl_pin, int sda_pin, uint8_t addr,
                      const uint8_t *tx_buf, size_t tx_len,
                      uint8_t *rx_buf, size_t rx_len);

/**
 * @brief Query current I2C subsystem status.
 */
int hw_i2c_get_status(hw_i2c_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_HW_I2C_H_ */
