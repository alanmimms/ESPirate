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

static int get_gpio_port_and_pin(int pin, const struct device **dev_out, gpio_pin_t *pin_out)
{
    if (pin < 0 || pin > 48) {
        return -EINVAL;
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
    } else if (strcmp(mode, "open_drain") == 0) {
        flags = GPIO_OUTPUT | GPIO_OPEN_DRAIN;
    } else {
        return luaL_error(L, "unknown mode '%s' (expected 'out', 'in', 'in_pullup', 'in_pulldown', 'open_drain')", mode);
    }

    ret = gpio_pin_configure(dev, pin_idx, flags);
    if (ret != 0) {
        return luaL_error(L, "gpio_pin_configure failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_write(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);

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
    const struct device *dev;
    gpio_pin_t pin_idx;
    if (get_gpio_port_and_pin(pin, &dev, &pin_idx) != 0) {
        return luaL_error(L, "invalid pin %d", pin);
    }
    gpio_pin_set(dev, pin_idx, 0);
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg gpio_funcs[] = {
    {"mode",   l_gpio_mode},
    {"write",  l_gpio_write},
    {"read",   l_gpio_read},
    {"toggle", l_gpio_toggle},
    {"high",   l_gpio_high},
    {"low",    l_gpio_low},
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

    if (pin < 0 || pin > 48) {
        return luaL_error(L, "invalid pin %d", pin);
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

    if (pin < 0 || pin > 48) {
        return luaL_error(L, "invalid pin %d", pin);
    }

    esp_rom_gpio_pad_select_gpio((uint32_t)pin);
    esp_rom_gpio_connect_in_signal((uint32_t)pin, (uint32_t)sig_idx, inv);

    lua_pushboolean(L, 1);
    return 1;
}

static int l_matrix_detach(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    if (pin < 0 || pin > 48) {
        return luaL_error(L, "invalid pin %d", pin);
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
