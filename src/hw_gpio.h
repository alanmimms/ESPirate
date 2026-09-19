/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_HW_GPIO_H_
#define ESPIRATE_HW_GPIO_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int pin;
    bool valid;
    bool reserved;
    bool input;
    bool output;
    bool pullup;
    bool pulldown;
    bool analog;
    const char *adc_name;
    const char *desc;
} hw_gpio_info_t;

/**
 * @brief Initialize underlying GPIO controller devices.
 */
int hw_gpio_init(void);

/**
 * @brief Validate pin number against ESP32-S3 silicon and system reservations.
 * @param pin Pin number (0..48).
 * @param reason Output pointer to human-readable explanation if unsafe.
 * @return 0 if safe, negative errno if invalid or reserved.
 */
int hw_gpio_check_safety(int pin, const char **reason);

/**
 * @brief Configure GPIO direction and optional pull mode.
 * @param pin Pin number.
 * @param mode "in", "out", "open_drain" (or "od"), "tristate" (or "hiz").
 * @param pull Optional "up", "down", or "none" (may be NULL).
 * @return 0 on success, negative errno on failure.
 */
int hw_gpio_mode(int pin, const char *mode, const char *pull);

/**
 * @brief Configure pull resistor without modifying direction.
 * @param pin Pin number.
 * @param pull "up", "down", or "none".
 * @return 0 on success, negative errno on failure.
 */
int hw_gpio_pull(int pin, const char *pull);

/**
 * @brief Set pin to high-impedance / disconnected mode (tristate).
 * @param pin Pin number.
 * @return 0 on success, negative errno on failure.
 */
int hw_gpio_tristate(int pin);

/**
 * @brief Write digital output value.
 * @param pin Pin number.
 * @param val 0 for LOW, non-zero for HIGH.
 * @return 0 on success, negative errno on failure.
 */
int hw_gpio_write(int pin, int val);

/**
 * @brief Sense digital input value.
 * @param pin Pin number.
 * @return 0 or 1 on success, negative errno on failure.
 */
int hw_gpio_read(int pin);

/**
 * @brief Toggle digital output pin.
 * @param pin Pin number.
 * @return 0 on success, negative errno on failure.
 */
int hw_gpio_toggle(int pin);

/**
 * @brief Drive digital pin HIGH (3.3V).
 */
int hw_gpio_high(int pin);

/**
 * @brief Drive digital pin LOW (0V).
 */
int hw_gpio_low(int pin);

/**
 * @brief Set pad drive strength (5, 10, 20, or 40 mA).
 */
int hw_gpio_drive(int pin, int ma);

/**
 * @brief Query pin capabilities and metadata.
 */
int hw_gpio_get_info(int pin, hw_gpio_info_t *info);

/**
 * @brief Query currently configured state and level of a pin.
 * @param pin Pin number.
 * @param mode_buf Buffer to receive mode string (e.g. "OUT (PUSH_PULL)", "IN (PULLUP)", "TRISTATE").
 * @param mode_size Size of mode_buf.
 * @param level Pointer to receive current sensed logic level (0 or 1).
 * @return 0 on success, negative errno on failure.
 */
int hw_gpio_get_state(int pin, char *mode_buf, size_t mode_size, int *level);

/**
 * @brief Route internal peripheral output signal to a physical GPIO pad via GPIO Matrix.
 */
int hw_matrix_route_out(int pin, int sig_idx, bool out_inv, bool oen_inv);

/**
 * @brief Route a physical GPIO pad into an internal peripheral input signal via GPIO Matrix.
 */
int hw_matrix_route_in(int pin, int sig_idx, bool inv);

/**
 * @brief Detach GPIO Matrix routing from pin back to standard GPIO.
 */
int hw_matrix_detach(int pin);

/**
 * @brief Get human-readable signal name from GPIO matrix signal index.
 */
const char *hw_matrix_get_signal_name(int sig_idx);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_HW_GPIO_H_ */
