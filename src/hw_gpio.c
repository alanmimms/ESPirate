/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

#include "hw_gpio.h"

LOG_MODULE_REGISTER(hw_gpio, LOG_LEVEL_INF);

static const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

static gpio_flags_t s_pin_flags[49] = {0};
static int s_pin_out_sig[49] = {0};
static int s_pin_in_sig[49] = {0};

int hw_gpio_init(void)
{
    if (!device_is_ready(gpio0_dev)) {
        LOG_WRN("gpio0 device not ready");
    }
    if (!device_is_ready(gpio1_dev)) {
        LOG_WRN("gpio1 device not ready");
    }
    for (int i = 0; i < 49; i++) {
        s_pin_flags[i] = GPIO_DISCONNECTED;
        s_pin_out_sig[i] = SIG_GPIO_OUT_IDX;
        s_pin_in_sig[i] = -1;
    }
    return 0;
}

int hw_gpio_check_safety(int pin, const char **reason)
{
    if (pin < 0 || pin > 48) {
        if (reason) *reason = "out of range (0..48)";
        return -EINVAL;
    }
    if (pin >= 22 && pin <= 25) {
        if (reason) *reason = "not bonded on ESP32-S3 silicon";
        return -EINVAL;
    }
    if (pin == 19 || pin == 20) {
        if (reason) *reason = "reserved for native USB OTG console (CDC-ACM)";
        return -EPERM;
    }
    if (pin >= 26 && pin <= 32) {
        if (reason) *reason = "reserved for Octal SPI Flash/PSRAM bus";
        return -EPERM;
    }
    if (pin >= 33 && pin <= 37) {
        if (reason) *reason = "reserved for Octal PSRAM data lines";
        return -EPERM;
    }
    return 0;
}

static int get_gpio_port_and_pin(int pin, const struct device **dev_out, gpio_pin_t *pin_out)
{
    const char *reason = NULL;
    int ret = hw_gpio_check_safety(pin, &reason);
    if (ret != 0) {
        return ret;
    }
    if (pin < 32) {
        *dev_out = gpio0_dev;
        *pin_out = (gpio_pin_t)pin;
    } else {
        *dev_out = gpio1_dev;
        *pin_out = (gpio_pin_t)(pin - 32);
    }
    if (!device_is_ready(*dev_out)) {
        return -ENODEV;
    }
    return 0;
}

int hw_gpio_mode(int pin, const char *mode, const char *pull)
{
    if (!mode) return -EINVAL;

    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) return ret;

    gpio_flags_t flags = GPIO_DISCONNECTED;
    if (strcmp(mode, "out") == 0 || strcmp(mode, "output") == 0) {
        flags = GPIO_OUTPUT | GPIO_INPUT;
    } else if (strcmp(mode, "in") == 0 || strcmp(mode, "input") == 0) {
        flags = GPIO_INPUT;
    } else if (strcmp(mode, "in_pullup") == 0 || strcmp(mode, "pullup") == 0) {
        flags = GPIO_INPUT | GPIO_PULL_UP;
    } else if (strcmp(mode, "in_pulldown") == 0 || strcmp(mode, "pulldown") == 0) {
        flags = GPIO_INPUT | GPIO_PULL_DOWN;
    } else if (strcmp(mode, "out_pullup") == 0) {
        flags = GPIO_OUTPUT | GPIO_INPUT | GPIO_PULL_UP;
    } else if (strcmp(mode, "out_pulldown") == 0) {
        flags = GPIO_OUTPUT | GPIO_INPUT | GPIO_PULL_DOWN;
    } else if (strcmp(mode, "open_drain") == 0 || strcmp(mode, "od") == 0) {
        flags = GPIO_OUTPUT | GPIO_INPUT | GPIO_OPEN_DRAIN;
    } else if (strcmp(mode, "open_drain_pullup") == 0 || strcmp(mode, "od_pullup") == 0) {
        flags = GPIO_OUTPUT | GPIO_INPUT | GPIO_OPEN_DRAIN | GPIO_PULL_UP;
    } else if (strcmp(mode, "tristate") == 0 || strcmp(mode, "hiz") == 0 ||
               strcmp(mode, "hi_z") == 0 || strcmp(mode, "disconnected") == 0 ||
               strcmp(mode, "none") == 0) {
        flags = GPIO_DISCONNECTED;
    } else {
        return -EINVAL;
    }

    if (pull) {
        flags &= ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
        if (strcmp(pull, "up") == 0 || strcmp(pull, "pullup") == 0) {
            flags |= GPIO_PULL_UP;
        } else if (strcmp(pull, "down") == 0 || strcmp(pull, "pulldown") == 0) {
            flags |= GPIO_PULL_DOWN;
        } else if (strcmp(pull, "none") == 0 || strcmp(pull, "floating") == 0 || strcmp(pull, "off") == 0) {
            /* clear pulls */
        } else {
            return -EINVAL;
        }
    }

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    ret = gpio_pin_configure(dev, pin_idx, flags);
    if (ret != 0) return ret;

    s_pin_flags[pin] = flags;
    return 0;
}

int hw_gpio_pull(int pin, const char *pull)
{
    if (!pull) return -EINVAL;

    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) return ret;

    gpio_flags_t flags = s_pin_flags[pin];
    if (flags == GPIO_DISCONNECTED && (flags & (GPIO_INPUT | GPIO_OUTPUT)) == 0) {
        flags = GPIO_INPUT;
    }

    flags &= ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
    if (strcmp(pull, "up") == 0 || strcmp(pull, "pullup") == 0) {
        flags |= GPIO_PULL_UP;
    } else if (strcmp(pull, "down") == 0 || strcmp(pull, "pulldown") == 0) {
        flags |= GPIO_PULL_DOWN;
    } else if (strcmp(pull, "none") == 0 || strcmp(pull, "floating") == 0 || strcmp(pull, "off") == 0) {
        /* clear pulls */
    } else {
        return -EINVAL;
    }

    ret = gpio_pin_configure(dev, pin_idx, flags);
    if (ret != 0) return ret;

    s_pin_flags[pin] = flags;
    return 0;
}

int hw_gpio_tristate(int pin)
{
    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) return ret;

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    ret = gpio_pin_configure(dev, pin_idx, GPIO_DISCONNECTED);
    if (ret != 0) return ret;

    s_pin_flags[pin] = GPIO_DISCONNECTED;
    return 0;
}

int hw_gpio_write(int pin, int val)
{
    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) return ret;

    /* Ensure output mode if currently disconnected */
    if ((s_pin_flags[pin] & GPIO_OUTPUT) == 0) {
        hw_gpio_mode(pin, "out", NULL);
    }

    return gpio_pin_set(dev, pin_idx, val ? 1 : 0);
}

int hw_gpio_read(int pin)
{
    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) return ret;

    /* Ensure input buffer is active if currently disconnected */
    if ((s_pin_flags[pin] & GPIO_INPUT) == 0) {
        hw_gpio_mode(pin, "in", NULL);
    }

    return gpio_pin_get(dev, pin_idx);
}

int hw_gpio_toggle(int pin)
{
    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) return ret;

    if ((s_pin_flags[pin] & GPIO_OUTPUT) == 0) {
        hw_gpio_mode(pin, "out", NULL);
    }

    return gpio_pin_toggle(dev, pin_idx);
}

int hw_gpio_high(int pin)
{
    return hw_gpio_write(pin, 1);
}

int hw_gpio_low(int pin)
{
    return hw_gpio_write(pin, 0);
}

int hw_gpio_drive(int pin, int ma)
{
    const char *reason = NULL;
    int ret = hw_gpio_check_safety(pin, &reason);
    if (ret != 0) return ret;

    uint32_t drv = 2; /* default 20mA */
    if (ma == 5 || ma == 0) drv = 0;
    else if (ma == 10 || ma == 1) drv = 1;
    else if (ma == 20 || ma == 2) drv = 2;
    else if (ma == 40 || ma == 3) drv = 3;
    else return -EINVAL;

    esp_rom_gpio_pad_set_drv((uint32_t)pin, drv);
    return 0;
}

int hw_gpio_get_info(int pin, hw_gpio_info_t *info)
{
    if (!info || pin < 0 || pin > 48) return -EINVAL;

    info->pin = pin;
    info->valid = (pin < 22 || pin > 25);
    info->reserved = (!info->valid || pin == 19 || pin == 20 || (pin >= 26 && pin <= 37));
    info->input = (info->valid && !info->reserved);
    info->output = (info->valid && !info->reserved);
    info->pullup = (info->valid && !info->reserved);
    info->pulldown = (info->valid && !info->reserved);

    static char adc_buf[16];
    if (pin >= 1 && pin <= 10) {
        snprintf(adc_buf, sizeof(adc_buf), "ADC1_CH%d", pin - 1);
        info->adc_name = adc_buf;
        info->analog = !info->reserved;
    } else if (pin >= 11 && pin <= 20) {
        snprintf(adc_buf, sizeof(adc_buf), "ADC2_CH%d", pin - 11);
        info->adc_name = adc_buf;
        info->analog = !info->reserved;
    } else {
        info->adc_name = NULL;
        info->analog = false;
    }

    if (!info->valid) {
        info->desc = "Not bonded on ESP32-S3 silicon";
    } else if (pin == 0) {
        info->desc = "Strapping Pin (BOOT button)";
    } else if (pin == 19) {
        info->desc = "Reserved: Native USB OTG D- (Console)";
    } else if (pin == 20) {
        info->desc = "Reserved: Native USB OTG D+ (Console)";
    } else if (pin >= 26 && pin <= 32) {
        info->desc = "Reserved: Octal SPI Flash / PSRAM";
    } else if (pin >= 33 && pin <= 37) {
        info->desc = "Reserved: Octal PSRAM data lines";
    } else if (pin == 43) {
        info->desc = "UART0 TXD (Console UART)";
    } else if (pin == 44) {
        info->desc = "UART0 RXD (Console UART)";
    } else if (pin == 45) {
        info->desc = "Strapping Pin (VDD_SPI voltage)";
    } else if (pin == 46) {
        info->desc = "Strapping Pin (Boot Mode / ROM log)";
    } else {
        info->desc = "General Purpose I/O";
    }

    return 0;
}

int hw_gpio_get_state(int pin, char *mode_buf, size_t mode_size, int *level)
{
    const char *reason = NULL;
    int ret = hw_gpio_check_safety(pin, &reason);
    if (ret != 0) {
        if (mode_buf && mode_size > 0) {
            snprintf(mode_buf, mode_size, "RESERVED (%s)", reason ? reason : "unsafe");
        }
        if (level) *level = -1;
        return ret;
    }

    gpio_flags_t flags = s_pin_flags[pin];
    const char *dir_str = "TRISTATE";
    if (flags & GPIO_OPEN_DRAIN) {
        dir_str = "OPEN_DRAIN";
    } else if ((flags & (GPIO_OUTPUT | GPIO_INPUT)) == (GPIO_OUTPUT | GPIO_INPUT)) {
        dir_str = "OUT (PUSH_PULL)";
    } else if (flags & GPIO_OUTPUT) {
        dir_str = "OUTPUT";
    } else if (flags & GPIO_INPUT) {
        dir_str = "INPUT";
    }

    const char *pull_str = "FLOATING";
    if (flags & GPIO_PULL_UP) {
        pull_str = "PULLUP";
    } else if (flags & GPIO_PULL_DOWN) {
        pull_str = "PULLDOWN";
    }

    if (mode_buf && mode_size > 0) {
        if (s_pin_out_sig[pin] != SIG_GPIO_OUT_IDX) {
            snprintf(mode_buf, mode_size, "%s [%s] -> Matrix(Sig %d: %s)",
                     dir_str, pull_str, s_pin_out_sig[pin],
                     hw_matrix_get_signal_name(s_pin_out_sig[pin]));
        } else {
            snprintf(mode_buf, mode_size, "%s [%s]", dir_str, pull_str);
        }
    }

    if (level) {
        const struct device *dev;
        gpio_pin_t pin_idx;
        if (get_gpio_port_and_pin(pin, &dev, &pin_idx) == 0) {
            *level = gpio_pin_get(dev, pin_idx);
        } else {
            *level = -1;
        }
    }
    return 0;
}

int hw_matrix_route_out(int pin, int sig_idx, bool out_inv, bool oen_inv)
{
    const char *reason = NULL;
    int ret = hw_gpio_check_safety(pin, &reason);
    if (ret != 0) return ret;

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    esp_rom_gpio_connect_out_signal((uint32_t)pin, (uint32_t)sig_idx, out_inv, oen_inv);
    s_pin_out_sig[pin] = sig_idx;
    return 0;
}

int hw_matrix_route_in(int pin, int sig_idx, bool inv)
{
    const char *reason = NULL;
    int ret = hw_gpio_check_safety(pin, &reason);
    if (ret != 0) return ret;

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    esp_rom_gpio_connect_in_signal((uint32_t)pin, (uint32_t)sig_idx, inv);
    s_pin_in_sig[pin] = sig_idx;
    return 0;
}

int hw_matrix_detach(int pin)
{
    const char *reason = NULL;
    int ret = hw_gpio_check_safety(pin, &reason);
    if (ret != 0) return ret;

    esp_rom_gpio_connect_out_signal((uint32_t)pin, SIG_GPIO_OUT_IDX, false, false);
    s_pin_out_sig[pin] = SIG_GPIO_OUT_IDX;
    s_pin_in_sig[pin] = -1;
    return 0;
}

const char *hw_matrix_get_signal_name(int sig_idx)
{
    switch (sig_idx) {
        case SIG_GPIO_OUT_IDX:          return "GPIO_OUT (Default)";
        case U0TXD_OUT_IDX:             return "U0TXD (UART0 Console TX)";
        case U1TXD_OUT_IDX:             return "U1TXD (UART1 TX)";
        case U2TXD_OUT_IDX:             return "U2TXD (UART2 TX)";
        case I2CEXT0_SCL_OUT_IDX:       return "I2C0_SCL (I2C0 Clock)";
        case I2CEXT0_SDA_OUT_IDX:       return "I2C0_SDA (I2C0 Data)";
        case I2CEXT1_SCL_OUT_IDX:       return "I2C1_SCL (I2C1 Clock)";
        case I2CEXT1_SDA_OUT_IDX:       return "I2C1_SDA (I2C1 Data)";
        case FSPICLK_OUT_IDX:           return "FSPICLK (SPI2 Clock)";
        case FSPID_OUT_IDX:             return "FSPID (SPI2 MOSI)";
        case FSPICS0_OUT_IDX:           return "FSPICS0 (SPI2 CS0)";
        case LEDC_LS_SIG_OUT0_IDX:      return "LEDC_OUT0 (PWM Channel 0)";
        case LEDC_LS_SIG_OUT1_IDX:      return "LEDC_OUT1 (PWM Channel 1)";
        case LEDC_LS_SIG_OUT2_IDX:      return "LEDC_OUT2 (PWM Channel 2)";
        case LEDC_LS_SIG_OUT3_IDX:      return "LEDC_OUT3 (PWM Channel 3)";
        case LEDC_LS_SIG_OUT4_IDX:      return "LEDC_OUT4 (PWM Channel 4)";
        case LEDC_LS_SIG_OUT5_IDX:      return "LEDC_OUT5 (PWM Channel 5)";
        case LEDC_LS_SIG_OUT6_IDX:      return "LEDC_OUT6 (PWM Channel 6)";
        case LEDC_LS_SIG_OUT7_IDX:      return "LEDC_OUT7 (PWM Channel 7)";
        default:                        return "UNKNOWN";
    }
}
