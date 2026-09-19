/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

#include <soc/system_struct.h>
#include <soc/ledc_struct.h>
#include <soc/gpio_sig_map.h>
#include <esp_rom_gpio.h>

#include "hw_gpio.h"
#include "hw_pwm.h"

LOG_MODULE_REGISTER(hw_pwm, LOG_LEVEL_INF);

static struct {
    int pin;
    uint32_t freq_hz;
    uint32_t duty_percent;
    bool active;
} s_channels[HW_PWM_MAX_CHANNELS];

static bool s_initialized = false;

int hw_pwm_init(void)
{
    if (s_initialized) return 0;

    /* Enable LEDC peripheral bus clock */
    SYSTEM.perip_clk_en0.ledc_clk_en = 1;
    SYSTEM.perip_rst_en0.ledc_rst = 0;

    for (int i = 0; i < HW_PWM_MAX_CHANNELS; i++) {
        s_channels[i].pin = -1;
        s_channels[i].freq_hz = 0;
        s_channels[i].duty_percent = 0;
        s_channels[i].active = false;
    }

    s_initialized = true;
    LOG_INF("LEDC hardware PWM subsystem initialized (8 channels)");
    return 0;
}

int hw_pwm_set(int pin, uint32_t freq_hz, uint32_t duty_percent)
{
    const char *reason = NULL;
    int ret = hw_gpio_check_safety(pin, &reason);
    if (ret != 0) {
        LOG_ERR("Pin %d safety violation: %s", pin, reason ? reason : "unsafe");
        return ret;
    }

    if (freq_hz == 0 || freq_hz > 40000000) {
        return -EINVAL;
    }
    if (duty_percent > 100) {
        duty_percent = 100;
    }

    if (!s_initialized) {
        hw_pwm_init();
    }

    /* Check if this pin is already assigned a channel */
    int ch = -1;
    for (int i = 0; i < HW_PWM_MAX_CHANNELS; i++) {
        if (s_channels[i].active && s_channels[i].pin == pin) {
            ch = i;
            break;
        }
    }

    /* If not assigned, find an inactive channel */
    if (ch < 0) {
        for (int i = 0; i < HW_PWM_MAX_CHANNELS; i++) {
            if (!s_channels[i].active) {
                ch = i;
                break;
            }
        }
    }

    if (ch < 0) {
        LOG_ERR("All %d hardware PWM channels are currently in use", HW_PWM_MAX_CHANNELS);
        return -EBUSY;
    }

    /* Map timer: 4 timers available (0..3) */
    int timer_idx = ch % 4;

    /* Select optimal resolution bits based on frequency */
    uint32_t res_bits = 10;
    if (freq_hz < 80) {
        res_bits = 14;
    } else if (freq_hz > 300000) {
        res_bits = 4;
    } else if (freq_hz > 50000) {
        res_bits = 8;
    }

    uint32_t max_duty = (1U << res_bits) - 1;
    uint64_t div_q8 = ((uint64_t)80000000 * 256) / ((uint64_t)freq_hz * (1U << res_bits));
    if (div_q8 < 256) div_q8 = 256;
    if (div_q8 > (1023 * 256)) div_q8 = (1023 * 256);

    /* Configure timer */
    LEDC.timer_group[0].timer[timer_idx].conf.duty_resolution = res_bits;
    LEDC.timer_group[0].timer[timer_idx].conf.clock_divider = (uint32_t)div_q8;
    LEDC.timer_group[0].timer[timer_idx].conf.tick_sel = 1; /* APB_CLK 80MHz */
    LEDC.timer_group[0].timer[timer_idx].conf.rst = 1;
    LEDC.timer_group[0].timer[timer_idx].conf.rst = 0;
    LEDC.timer_group[0].timer[timer_idx].conf.low_speed_update = 1;

    /* Configure channel */
    uint32_t duty_val = (duty_percent * max_duty) / 100;
    LEDC.channel_group[0].channel[ch].conf0.timer_sel = timer_idx;
    LEDC.channel_group[0].channel[ch].conf0.sig_out_en = 1;
    LEDC.channel_group[0].channel[ch].hpoint.hpoint = 0;
    LEDC.channel_group[0].channel[ch].duty.duty = duty_val << 4;
    LEDC.channel_group[0].channel[ch].conf1.duty_start = 1;
    LEDC.channel_group[0].channel[ch].conf0.low_speed_update = 1;

    /* Route channel to GPIO pin via Matrix */
    hw_gpio_mode(pin, "out", NULL);
    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    esp_rom_gpio_connect_out_signal((uint32_t)pin, LEDC_LS_SIG_OUT0_IDX + ch, false, false);

    s_channels[ch].pin = pin;
    s_channels[ch].freq_hz = freq_hz;
    s_channels[ch].duty_percent = duty_percent;
    s_channels[ch].active = true;

    LOG_INF("PWM active on GPIO %d: Channel=%d, Freq=%u Hz, Duty=%u%%",
            pin, ch, freq_hz, duty_percent);
    return 0;
}

int hw_pwm_stop(int pin)
{
    if (!s_initialized) return -ENOENT;

    for (int i = 0; i < HW_PWM_MAX_CHANNELS; i++) {
        if (s_channels[i].active && s_channels[i].pin == pin) {
            LEDC.channel_group[0].channel[i].conf0.sig_out_en = 0;
            LEDC.channel_group[0].channel[i].conf0.low_speed_update = 1;

            /* Restore GPIO routing */
            hw_matrix_detach(pin);
            hw_gpio_tristate(pin);

            s_channels[i].active = false;
            s_channels[i].pin = -1;
            s_channels[i].freq_hz = 0;
            s_channels[i].duty_percent = 0;

            LOG_INF("PWM stopped on GPIO %d (Channel %d released)", pin, i);
            return 0;
        }
    }
    return -ENOENT;
}

int hw_pwm_get_pin_status(int pin, hw_pwm_status_t *status)
{
    if (!status || !s_initialized) return -ENOENT;

    for (int i = 0; i < HW_PWM_MAX_CHANNELS; i++) {
        if (s_channels[i].active && s_channels[i].pin == pin) {
            status->channel = i;
            status->pin = pin;
            status->freq_hz = s_channels[i].freq_hz;
            status->duty_percent = s_channels[i].duty_percent;
            status->active = true;
            return 0;
        }
    }
    return -ENOENT;
}

int hw_pwm_get_all_status(hw_pwm_status_t *list, size_t max_entries, size_t *count)
{
    if (!s_initialized) {
        if (count) *count = 0;
        return 0;
    }

    size_t n = 0;
    for (int i = 0; i < HW_PWM_MAX_CHANNELS; i++) {
        if (s_channels[i].active) {
            if (list && n < max_entries) {
                list[n].channel = i;
                list[n].pin = s_channels[i].pin;
                list[n].freq_hz = s_channels[i].freq_hz;
                list[n].duty_percent = s_channels[i].duty_percent;
                list[n].active = true;
            }
            n++;
        }
    }

    if (count) *count = n;
    return 0;
}
