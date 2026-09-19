/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_HW_SPI_H_
#define ESPIRATE_HW_SPI_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sck_pin;
    int mosi_pin;
    int miso_pin;
    int cs_pin;
    uint32_t freq_khz;
    uint8_t mode; /* 0..3 */
    bool active;
} hw_spi_status_t;

/**
 * @brief Initialize/Configure the SPI bus pins and mode.
 */
int hw_spi_init_bus(int sck_pin, int mosi_pin, int miso_pin, int cs_pin,
                    uint32_t freq_khz, uint8_t mode);

/**
 * @brief Perform full-duplex SPI transfer.
 * @param sck_pin Clock pin.
 * @param mosi_pin MOSI pin (-1 if write not needed).
 * @param miso_pin MISO pin (-1 if read not needed).
 * @param cs_pin Chip select pin (-1 if unmanaged).
 * @param mode SPI mode (0..3).
 * @param tx Buffer containing data to send (or NULL to send 0xFF).
 * @param rx Buffer to receive data (or NULL to discard).
 * @param len Number of bytes to transfer.
 * @return 0 on success, negative errno on failure.
 */
int hw_spi_transfer(int sck_pin, int mosi_pin, int miso_pin, int cs_pin,
                    uint8_t mode, const uint8_t *tx, uint8_t *rx, size_t len);

/**
 * @brief Write bytes over SPI.
 */
int hw_spi_write(int sck_pin, int mosi_pin, int cs_pin, uint8_t mode,
                 const uint8_t *tx, size_t len);

/**
 * @brief Read bytes over SPI.
 */
int hw_spi_read(int sck_pin, int miso_pin, int cs_pin, uint8_t mode,
                uint8_t *rx, size_t len);

/**
 * @brief Query current SPI subsystem status.
 */
int hw_spi_get_status(hw_spi_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_HW_SPI_H_ */
