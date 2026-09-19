/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua_gpio.h"

LOG_MODULE_REGISTER(lua_gpio, LOG_LEVEL_INF);

static const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

int espirate_hw_gpio_init(void)
{
    if (!device_is_ready(gpio0_dev)) {
        LOG_WRN("gpio0 device not ready");
    }
    if (!device_is_ready(gpio1_dev)) {
        LOG_WRN("gpio1 device not ready");
    }
    return 0;
}

static gpio_flags_t s_pin_flags[49] = {0};

static int check_pin_safety(int pin, const char **reason)
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
    int ret = check_pin_safety(pin, &reason);
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

/* ========================================================================= */
/*                              'gpio' MODULE                                */
/* ========================================================================= */

static int l_gpio_mode(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *mode = luaL_checkstring(L, 2);
    const char *pull = lua_tostring(L, 3);

    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }

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
        return luaL_error(L, "unknown mode '%s' (expected 'in', 'out', 'open_drain', 'tristate')", mode);
    }

    if (pull) {
        flags &= ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
        if (strcmp(pull, "up") == 0 || strcmp(pull, "pullup") == 0) {
            flags |= GPIO_PULL_UP;
        } else if (strcmp(pull, "down") == 0 || strcmp(pull, "pulldown") == 0) {
            flags |= GPIO_PULL_DOWN;
        } else if (strcmp(pull, "none") == 0 || strcmp(pull, "floating") == 0 || strcmp(pull, "off") == 0) {
            /* cleared */
        } else {
            return luaL_error(L, "unknown pull '%s' (expected 'up', 'down', 'none')", pull);
        }
    }

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    ret = gpio_pin_configure(dev, pin_idx, flags);
    if (ret != 0) {
        return luaL_error(L, "gpio_pin_configure failed: %d", ret);
    }

    s_pin_flags[pin] = flags;
    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_pull(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *pull = luaL_checkstring(L, 2);

    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }

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
        /* cleared */
    } else {
        return luaL_error(L, "unknown pull mode '%s' (expected 'up', 'down', 'none')", pull);
    }

    ret = gpio_pin_configure(dev, pin_idx, flags);
    if (ret != 0) {
        return luaL_error(L, "gpio_pin_configure failed: %d", ret);
    }

    s_pin_flags[pin] = flags;
    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_tristate(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    ret = gpio_pin_configure(dev, pin_idx, GPIO_DISCONNECTED);
    if (ret != 0) {
        return luaL_error(L, "gpio_pin_configure failed: %d", ret);
    }

    s_pin_flags[pin] = GPIO_DISCONNECTED;
    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_write(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);

    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }

    ret = gpio_pin_set(dev, pin_idx, val ? 1 : 0);
    if (ret != 0) {
        return luaL_error(L, "gpio_pin_set failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_read(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);

    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }

    int val = gpio_pin_get(dev, pin_idx);
    if (val < 0) {
        return luaL_error(L, "gpio_pin_get failed: %d", val);
    }

    lua_pushinteger(L, val);
    return 1;
}

static int l_gpio_toggle(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);

    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    const struct device *dev;
    gpio_pin_t pin_idx;
    int ret = get_gpio_port_and_pin(pin, &dev, &pin_idx);
    if (ret != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }

    ret = gpio_pin_toggle(dev, pin_idx);
    if (ret != 0) {
        return luaL_error(L, "gpio_pin_toggle failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_high(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }
    const struct device *dev;
    gpio_pin_t pin_idx;
    if (get_gpio_port_and_pin(pin, &dev, &pin_idx) != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }
    gpio_pin_set(dev, pin_idx, 1);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_low(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }
    const struct device *dev;
    gpio_pin_t pin_idx;
    if (get_gpio_port_and_pin(pin, &dev, &pin_idx) != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }
    gpio_pin_set(dev, pin_idx, 0);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_drive(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int ma = (int)luaL_checkinteger(L, 2);
    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    uint32_t drv = 2; /* default 20mA */
    if (ma == 5 || ma == 0) drv = 0;
    else if (ma == 10 || ma == 1) drv = 1;
    else if (ma == 20 || ma == 2) drv = 2;
    else if (ma == 40 || ma == 3) drv = 3;
    else return luaL_error(L, "invalid drive strength %d (expected 5, 10, 20, or 40 mA)", ma);

    esp_rom_gpio_pad_set_drv((uint32_t)pin, drv);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_info(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    if (pin < 0 || pin > 48) {
        return luaL_error(L, "pin out of range (0..48)");
    }

    lua_newtable(L);
    lua_pushinteger(L, pin);
    lua_setfield(L, -2, "pin");

    bool valid = (pin < 22 || pin > 25);
    bool reserved = (!valid || pin == 19 || pin == 20 || (pin >= 26 && pin <= 37));

    lua_pushboolean(L, valid);
    lua_setfield(L, -2, "valid");

    lua_pushboolean(L, reserved);
    lua_setfield(L, -2, "reserved");

    lua_pushboolean(L, valid && !reserved);
    lua_setfield(L, -2, "input");

    lua_pushboolean(L, valid && !reserved);
    lua_setfield(L, -2, "output");

    lua_pushboolean(L, valid && !reserved);
    lua_setfield(L, -2, "pullup");

    lua_pushboolean(L, valid && !reserved);
    lua_setfield(L, -2, "pulldown");

    /* ADC channel mapping */
    static char adc_buf[16];
    const char *adc_name = NULL;
    if (pin >= 1 && pin <= 10) {
        snprintf(adc_buf, sizeof(adc_buf), "ADC1_CH%d", pin - 1);
        adc_name = adc_buf;
    } else if (pin >= 11 && pin <= 20) {
        snprintf(adc_buf, sizeof(adc_buf), "ADC2_CH%d", pin - 11);
        adc_name = adc_buf;
    }

    if (adc_name && !reserved) {
        lua_pushstring(L, adc_name);
        lua_setfield(L, -2, "adc");
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "analog");
    } else {
        lua_pushnil(L);
        lua_setfield(L, -2, "adc");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "analog");
    }

    const char *desc = "General Purpose I/O";
    if (!valid) {
        desc = "Not bonded on ESP32-S3 silicon";
    } else if (pin == 0) {
        desc = "Strapping Pin (BOOT button)";
    } else if (pin == 19) {
        desc = "Reserved: Native USB OTG D- (Console)";
    } else if (pin == 20) {
        desc = "Reserved: Native USB OTG D+ (Console)";
    } else if (pin >= 26 && pin <= 32) {
        desc = "Reserved: Octal SPI Flash / PSRAM";
    } else if (pin >= 33 && pin <= 37) {
        desc = "Reserved: Octal PSRAM data lines";
    } else if (pin == 43) {
        desc = "UART0 TXD (Console UART)";
    } else if (pin == 44) {
        desc = "UART0 RXD (Console UART)";
    } else if (pin == 45) {
        desc = "Strapping Pin (VDD_SPI voltage)";
    } else if (pin == 46) {
        desc = "Strapping Pin (Boot Mode / ROM log)";
    }

    lua_pushstring(L, desc);
    lua_setfield(L, -2, "desc");

    return 1;
}

static int l_gpio_list(lua_State *L)
{
    lua_newtable(L);
    int idx = 1;
    for (int p = 0; p <= 48; p++) {
        if (p >= 22 && p <= 25) continue;
        if (p == 19 || p == 20) continue;
        if (p >= 26 && p <= 37) continue;

        lua_pushinteger(L, p);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

static const luaL_Reg gpio_funcs[] = {
    {"mode",      l_gpio_mode},
    {"pull",      l_gpio_pull},
    {"tristate",  l_gpio_tristate},
    {"hiz",       l_gpio_tristate},
    {"write",     l_gpio_write},
    {"read",      l_gpio_read},
    {"get",       l_gpio_read},
    {"high",      l_gpio_high},
    {"set",       l_gpio_high},
    {"low",       l_gpio_low},
    {"clear",     l_gpio_low},
    {"toggle",    l_gpio_toggle},
    {"drive",     l_gpio_drive},
    {"info",      l_gpio_info},
    {"caps",      l_gpio_info},
    {"list",      l_gpio_list},
    {"pins",      l_gpio_list},
    {NULL, NULL}
};

/* ========================================================================= */
/*                             'matrix' MODULE                               */
/* ========================================================================= */

static int l_matrix_route_out(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int sig_idx = (int)luaL_checkinteger(L, 2);
    bool out_inv = lua_toboolean(L, 3);
    bool oen_inv = lua_toboolean(L, 4);

    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    esp_rom_gpio_connect_out_signal((uint32_t)pin, (uint32_t)sig_idx, out_inv, oen_inv);

    lua_pushboolean(L, 1);
    return 1;
}

static int l_matrix_route_in(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int sig_idx = (int)luaL_checkinteger(L, 2);
    bool inv = lua_toboolean(L, 3);

    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    esp_rom_gpio_connect_in_signal((uint32_t)pin, (uint32_t)sig_idx, inv);

    lua_pushboolean(L, 1);
    return 1;
}

static int l_matrix_detach(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *reason = NULL;
    if (check_pin_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    esp_rom_gpio_connect_out_signal((uint32_t)pin, SIG_GPIO_OUT_IDX, false, false);
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg matrix_funcs[] = {
    {"route_out", l_matrix_route_out},
    {"route_in",  l_matrix_route_in},
    {"detach",    l_matrix_detach},
    {NULL, NULL}
};

/* ========================================================================= */
/*                              'sys' MODULE                                 */
/* ========================================================================= */

static int l_sys_sleep(lua_State *L)
{
    int ms = (int)luaL_checkinteger(L, 1);
    if (ms > 0) {
        k_msleep(ms);
    }
    return 0;
}

static int l_sys_usleep(lua_State *L)
{
    int us = (int)luaL_checkinteger(L, 1);
    if (us > 0) {
        k_busy_wait(us);
    }
    return 0;
}

static int l_sys_uptime(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)k_uptime_get());
    return 1;
}

static int l_sys_cycles(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)k_cycle_get_64());
    return 1;
}

static const luaL_Reg sys_funcs[] = {
    {"sleep",    l_sys_sleep},
    {"sleep_ms", l_sys_sleep},
    {"msleep",   l_sys_sleep},
    {"usleep",   l_sys_usleep},
    {"uptime",   l_sys_uptime},
    {"cycles",   l_sys_cycles},
    {NULL, NULL}
};

/* ========================================================================= */
/*                         MODULE REGISTRATION                               */
/* ========================================================================= */

void luaopen_espirate_hardware(lua_State *L)
{
    /* 1. Register 'gpio' */
    luaL_newlib(L, gpio_funcs);
    lua_setglobal(L, "gpio");

    /* 2. Register 'matrix' */
    luaL_newlib(L, matrix_funcs);

    /* Export common hardware signal indices into matrix table */
    lua_pushinteger(L, SIG_GPIO_OUT_IDX);
    lua_setfield(L, -2, "SIG_GPIO_OUT");

    lua_pushinteger(L, U0TXD_OUT_IDX);
    lua_setfield(L, -2, "U0TXD");
    lua_pushinteger(L, U0RXD_IN_IDX);
    lua_setfield(L, -2, "U0RXD");

    lua_pushinteger(L, U1TXD_OUT_IDX);
    lua_setfield(L, -2, "U1TXD");
    lua_pushinteger(L, U1RXD_IN_IDX);
    lua_setfield(L, -2, "U1RXD");

    lua_pushinteger(L, U2TXD_OUT_IDX);
    lua_setfield(L, -2, "U2TXD");
    lua_pushinteger(L, U2RXD_IN_IDX);
    lua_setfield(L, -2, "U2RXD");

    lua_pushinteger(L, I2CEXT0_SCL_OUT_IDX);
    lua_setfield(L, -2, "I2C0_SCL");
    lua_pushinteger(L, I2CEXT0_SDA_OUT_IDX);
    lua_setfield(L, -2, "I2C0_SDA");

    lua_pushinteger(L, I2CEXT1_SCL_OUT_IDX);
    lua_setfield(L, -2, "I2C1_SCL");
    lua_pushinteger(L, I2CEXT1_SDA_OUT_IDX);
    lua_setfield(L, -2, "I2C1_SDA");

    lua_pushinteger(L, SPICLK_OUT_IDX);
    lua_setfield(L, -2, "SPICLK");
    lua_pushinteger(L, SPICS0_OUT_IDX);
    lua_setfield(L, -2, "SPICS0");
    lua_pushinteger(L, SPID_OUT_IDX);
    lua_setfield(L, -2, "SPID");
    lua_pushinteger(L, SPIQ_OUT_IDX);
    lua_setfield(L, -2, "SPIQ");
    lua_pushinteger(L, SPIWP_OUT_IDX);
    lua_setfield(L, -2, "SPIWP");
    lua_pushinteger(L, SPIHD_OUT_IDX);
    lua_setfield(L, -2, "SPIHD");

    lua_setglobal(L, "matrix");

    /* 3. Register 'sys' */
    luaL_newlib(L, sys_funcs);
    lua_setglobal(L, "sys");
}
