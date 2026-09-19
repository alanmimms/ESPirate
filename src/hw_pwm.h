/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_HW_PWM_H_
#define ESPIRATE_HW_PWM_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HW_PWM_MAX_CHANNELS 8

typedef struct {
    int channel;
    int pin;
    uint32_t freq_hz;
    uint32_t duty_percent;
    bool active;
} hw_pwm_status_t;

/**
 * @brief Initialize the hardware PWM (LEDC) subsystem.
 */
int hw_pwm_init(void);

/**
 * @brief Configure and start PWM output on a GPIO pin.
 * @param pin GPIO pin number (must pass safety check).
 * @param freq_hz Frequency in Hz (e.g. 100 to 40000000).
 * @param duty_percent Duty cycle percentage (0 to 100).
 * @return 0 on success, negative errno on failure.
 */
int hw_pwm_set(int pin, uint32_t freq_hz, uint32_t duty_percent);

/**
 * @brief Stop PWM output on a pin and release the LEDC channel.
 * @param pin GPIO pin number.
 * @return 0 on success, negative errno on failure.
 */
int hw_pwm_stop(int pin);

/**
 * @brief Query status of a specific PWM pin.
 * @param pin GPIO pin number.
 * @param status Pointer to store status.
 * @return 0 if active, -ENOENT if pin is not running PWM.
 */
int hw_pwm_get_pin_status(int pin, hw_pwm_status_t *status);

/**
 * @brief Get list of all active PWM channels.
 * @param list Array of hw_pwm_status_t to receive active channels.
 * @param max_entries Maximum entries to store.
 * @param count Output pointer to receive number of active channels.
 * @return 0 on success.
 */
int hw_pwm_get_all_status(hw_pwm_status_t *list, size_t max_entries, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_HW_PWM_H_ */
