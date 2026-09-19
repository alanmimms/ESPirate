/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "hw_gpio.h"
#include "hw_pwm.h"
#include "hw_i2c.h"
#include "hw_spi.h"
#include "lua_gpio.h"

LOG_MODULE_REGISTER(lua_gpio, LOG_LEVEL_INF);

int espirate_hw_gpio_init(void)
{
    hw_gpio_init();
    hw_pwm_init();
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
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_gpio_mode(pin, mode, pull);
    if (ret != 0) {
        return luaL_error(L, "hw_gpio_mode failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_pull(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *pull = luaL_checkstring(L, 2);

    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_gpio_pull(pin, pull);
    if (ret != 0) {
        return luaL_error(L, "hw_gpio_pull failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_tristate(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_gpio_tristate(pin);
    if (ret != 0) {
        return luaL_error(L, "hw_gpio_tristate failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_write(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);

    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_gpio_write(pin, val);
    if (ret != 0) {
        return luaL_error(L, "hw_gpio_write failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_read(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);

    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int val = hw_gpio_read(pin);
    if (val < 0) {
        return luaL_error(L, "hw_gpio_read failed: %d", val);
    }

    lua_pushinteger(L, val);
    return 1;
}

static int l_gpio_toggle(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);

    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_gpio_toggle(pin);
    if (ret != 0) {
        return luaL_error(L, "hw_gpio_toggle failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_high(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_gpio_high(pin);
    if (ret != 0) {
        return luaL_error(L, "hw_gpio_high failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_low(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_gpio_low(pin);
    if (ret != 0) {
        return luaL_error(L, "hw_gpio_low failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_drive(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int ma = (int)luaL_checkinteger(L, 2);

    int ret = hw_gpio_drive(pin, ma);
    if (ret != 0) {
        return luaL_error(L, "invalid drive strength %d (expected 5, 10, 20, or 40 mA)", ma);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_gpio_info(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    hw_gpio_info_t info;
    int ret = hw_gpio_get_info(pin, &info);
    if (ret != 0) {
        return luaL_error(L, "pin out of range (0..48)");
    }

    lua_newtable(L);
    lua_pushinteger(L, info.pin);
    lua_setfield(L, -2, "pin");

    lua_pushboolean(L, info.valid);
    lua_setfield(L, -2, "valid");

    lua_pushboolean(L, info.reserved);
    lua_setfield(L, -2, "reserved");

    lua_pushboolean(L, info.input);
    lua_setfield(L, -2, "input");

    lua_pushboolean(L, info.output);
    lua_setfield(L, -2, "output");

    lua_pushboolean(L, info.pullup);
    lua_setfield(L, -2, "pullup");

    lua_pushboolean(L, info.pulldown);
    lua_setfield(L, -2, "pulldown");

    lua_pushboolean(L, info.analog);
    lua_setfield(L, -2, "analog");

    if (info.adc_name) {
        lua_pushstring(L, info.adc_name);
        lua_setfield(L, -2, "adc");
    } else {
        lua_pushnil(L);
        lua_setfield(L, -2, "adc");
    }

    lua_pushstring(L, info.desc ? info.desc : "");
    lua_setfield(L, -2, "desc");

    return 1;
}

static int l_gpio_list(lua_State *L)
{
    lua_newtable(L);
    int idx = 1;
    for (int p = 0; p <= 48; p++) {
        if (hw_gpio_check_safety(p, NULL) == 0) {
            lua_pushinteger(L, p);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

static int l_gpio_status(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    char mode_buf[64] = {0};
    int level = -1;

    int ret = hw_gpio_get_state(pin, mode_buf, sizeof(mode_buf), &level);
    if (ret != 0) {
        return luaL_error(L, "pin %d is invalid or reserved", pin);
    }

    lua_newtable(L);
    lua_pushinteger(L, pin);
    lua_setfield(L, -2, "pin");
    lua_pushstring(L, mode_buf);
    lua_setfield(L, -2, "mode");
    lua_pushinteger(L, level);
    lua_setfield(L, -2, "level");

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
    {"set",       l_gpio_write},
    {"low",       l_gpio_low},
    {"clear",     l_gpio_low},
    {"toggle",    l_gpio_toggle},
    {"drive",     l_gpio_drive},
    {"info",      l_gpio_info},
    {"caps",      l_gpio_info},
    {"list",      l_gpio_list},
    {"pins",      l_gpio_list},
    {"status",    l_gpio_status},
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
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_matrix_route_out(pin, sig_idx, out_inv, oen_inv);
    if (ret != 0) {
        return luaL_error(L, "hw_matrix_route_out failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_matrix_route_in(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int sig_idx = (int)luaL_checkinteger(L, 2);
    bool inv = lua_toboolean(L, 3);

    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_matrix_route_in(pin, sig_idx, inv);
    if (ret != 0) {
        return luaL_error(L, "hw_matrix_route_in failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_matrix_detach(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_matrix_detach(pin);
    if (ret != 0) {
        return luaL_error(L, "hw_matrix_detach failed: %d", ret);
    }

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
/*                              'pwm' MODULE                                 */
/* ========================================================================= */

static int l_pwm_set(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    uint32_t freq_hz = (uint32_t)luaL_checkinteger(L, 2);
    uint32_t duty_percent = (uint32_t)luaL_checkinteger(L, 3);

    const char *reason = NULL;
    if (hw_gpio_check_safety(pin, &reason) != 0) {
        return luaL_error(L, "pin %d is %s", pin, reason);
    }

    int ret = hw_pwm_set(pin, freq_hz, duty_percent);
    if (ret != 0) {
        return luaL_error(L, "hw_pwm_set failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_pwm_stop(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    int ret = hw_pwm_stop(pin);
    if (ret != 0) {
        return luaL_error(L, "hw_pwm_stop failed (pin %d not active): %d", pin, ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_pwm_status(lua_State *L)
{
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        int pin = (int)luaL_checkinteger(L, 1);
        hw_pwm_status_t st;
        int ret = hw_pwm_get_pin_status(pin, &st);
        if (ret != 0) {
            lua_pushnil(L);
            return 1;
        }

        lua_newtable(L);
        lua_pushinteger(L, st.channel);
        lua_setfield(L, -2, "channel");
        lua_pushinteger(L, st.pin);
        lua_setfield(L, -2, "pin");
        lua_pushinteger(L, st.freq_hz);
        lua_setfield(L, -2, "freq");
        lua_pushinteger(L, st.duty_percent);
        lua_setfield(L, -2, "duty");
        lua_pushboolean(L, st.active);
        lua_setfield(L, -2, "active");
        return 1;
    }

    /* Return table of all active channels */
    hw_pwm_status_t list[HW_PWM_MAX_CHANNELS];
    size_t count = 0;
    hw_pwm_get_all_status(list, HW_PWM_MAX_CHANNELS, &count);

    lua_newtable(L);
    for (size_t i = 0; i < count; i++) {
        lua_newtable(L);
        lua_pushinteger(L, list[i].channel);
        lua_setfield(L, -2, "channel");
        lua_pushinteger(L, list[i].pin);
        lua_setfield(L, -2, "pin");
        lua_pushinteger(L, list[i].freq_hz);
        lua_setfield(L, -2, "freq");
        lua_pushinteger(L, list[i].duty_percent);
        lua_setfield(L, -2, "duty");
        lua_pushboolean(L, list[i].active);
        lua_setfield(L, -2, "active");

        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

static const luaL_Reg pwm_funcs[] = {
    {"set",    l_pwm_set},
    {"stop",   l_pwm_stop},
    {"status", l_pwm_status},
    {NULL, NULL}
};

/* ========================================================================= */
/*                              'i2c' MODULE                                 */
/* ========================================================================= */

/* Helper: parse Lua args into byte buffer (table, string, or varargs) */
static size_t parse_lua_bytes(lua_State *L, int start_idx, uint8_t *buf, size_t max_buf)
{
    size_t len = 0;
    int top = lua_gettop(L);

    if (start_idx > top) return 0;

    if (lua_istable(L, start_idx)) {
        lua_Integer tlen = luaL_len(L, start_idx);
        for (lua_Integer i = 1; i <= tlen && len < max_buf; i++) {
            lua_rawgeti(L, start_idx, i);
            buf[len++] = (uint8_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
    } else if (lua_isstring(L, start_idx)) {
        size_t slen = 0;
        const char *s = lua_tolstring(L, start_idx, &slen);
        if (slen > max_buf) slen = max_buf;
        memcpy(buf, s, slen);
        len = slen;
    } else {
        for (int i = start_idx; i <= top && len < max_buf; i++) {
            buf[len++] = (uint8_t)luaL_checkinteger(L, i);
        }
    }
    return len;
}

static int l_i2c_scan(lua_State *L)
{
    int scl = (int)luaL_checkinteger(L, 1);
    int sda = (int)luaL_checkinteger(L, 2);

    uint8_t found[128];
    size_t count = 0;
    int ret = hw_i2c_scan(scl, sda, found, sizeof(found), &count);
    if (ret != 0) {
        return luaL_error(L, "hw_i2c_scan failed: %d", ret);
    }

    lua_newtable(L);
    for (size_t i = 0; i < count; i++) {
        lua_pushinteger(L, found[i]);
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

static int l_i2c_write(lua_State *L)
{
    int scl = (int)luaL_checkinteger(L, 1);
    int sda = (int)luaL_checkinteger(L, 2);
    uint8_t addr = (uint8_t)luaL_checkinteger(L, 3);

    uint8_t buf[256];
    size_t len = parse_lua_bytes(L, 4, buf, sizeof(buf));

    int ret = hw_i2c_write(scl, sda, addr, buf, len);
    if (ret != 0) {
        return luaL_error(L, "hw_i2c_write failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_i2c_read(lua_State *L)
{
    int scl = (int)luaL_checkinteger(L, 1);
    int sda = (int)luaL_checkinteger(L, 2);
    uint8_t addr = (uint8_t)luaL_checkinteger(L, 3);
    size_t len = (size_t)luaL_checkinteger(L, 4);

    if (len > 512) len = 512;
    uint8_t buf[512];

    int ret = hw_i2c_read(scl, sda, addr, buf, len);
    if (ret != 0) {
        return luaL_error(L, "hw_i2c_read failed: %d", ret);
    }

    lua_newtable(L);
    for (size_t i = 0; i < len; i++) {
        lua_pushinteger(L, buf[i]);
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

static int l_i2c_write_read(lua_State *L)
{
    int scl = (int)luaL_checkinteger(L, 1);
    int sda = (int)luaL_checkinteger(L, 2);
    uint8_t addr = (uint8_t)luaL_checkinteger(L, 3);

    uint8_t tx_buf[128];
    size_t tx_len = 0;

    if (lua_istable(L, 4)) {
        tx_len = parse_lua_bytes(L, 4, tx_buf, sizeof(tx_buf));
    } else {
        tx_buf[0] = (uint8_t)luaL_checkinteger(L, 4);
        tx_len = 1;
    }

    size_t rx_len = (size_t)luaL_checkinteger(L, 5);
    if (rx_len > 512) rx_len = 512;
    uint8_t rx_buf[512];

    int ret = hw_i2c_write_read(scl, sda, addr, tx_buf, tx_len, rx_buf, rx_len);
    if (ret != 0) {
        return luaL_error(L, "hw_i2c_write_read failed: %d", ret);
    }

    lua_newtable(L);
    for (size_t i = 0; i < rx_len; i++) {
        lua_pushinteger(L, rx_buf[i]);
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

static int l_i2c_status(lua_State *L)
{
    hw_i2c_status_t st;
    hw_i2c_get_status(&st);

    lua_newtable(L);
    lua_pushinteger(L, st.scl_pin);
    lua_setfield(L, -2, "scl");
    lua_pushinteger(L, st.sda_pin);
    lua_setfield(L, -2, "sda");
    lua_pushinteger(L, st.speed_khz);
    lua_setfield(L, -2, "speed_khz");
    lua_pushboolean(L, st.active);
    lua_setfield(L, -2, "active");

    lua_newtable(L);
    for (int i = 0; i < st.last_scanned_count; i++) {
        lua_pushinteger(L, st.last_scanned_addrs[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "last_scan");

    return 1;
}

static const luaL_Reg i2c_funcs[] = {
    {"scan",       l_i2c_scan},
    {"write",      l_i2c_write},
    {"read",       l_i2c_read},
    {"write_read", l_i2c_write_read},
    {"status",     l_i2c_status},
    {NULL, NULL}
};

/* ========================================================================= */
/*                              'spi' MODULE                                 */
/* ========================================================================= */

static int l_spi_transfer(lua_State *L)
{
    int sck  = (int)luaL_checkinteger(L, 1);
    int mosi = (int)luaL_checkinteger(L, 2);
    int miso = (int)luaL_checkinteger(L, 3);
    int cs   = (int)luaL_checkinteger(L, 4);
    uint8_t mode = (uint8_t)luaL_checkinteger(L, 5);

    uint8_t tx[256];
    size_t len = parse_lua_bytes(L, 6, tx, sizeof(tx));
    if (len == 0) len = 1;

    uint8_t rx[256];
    int ret = hw_spi_transfer(sck, mosi, miso, cs, mode, tx, rx, len);
    if (ret != 0) {
        return luaL_error(L, "hw_spi_transfer failed: %d", ret);
    }

    lua_newtable(L);
    for (size_t i = 0; i < len; i++) {
        lua_pushinteger(L, rx[i]);
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

static int l_spi_write(lua_State *L)
{
    int sck  = (int)luaL_checkinteger(L, 1);
    int mosi = (int)luaL_checkinteger(L, 2);
    int cs   = (int)luaL_checkinteger(L, 3);
    uint8_t mode = (uint8_t)luaL_checkinteger(L, 4);

    uint8_t tx[256];
    size_t len = parse_lua_bytes(L, 5, tx, sizeof(tx));

    int ret = hw_spi_write(sck, mosi, cs, mode, tx, len);
    if (ret != 0) {
        return luaL_error(L, "hw_spi_write failed: %d", ret);
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_spi_read(lua_State *L)
{
    int sck  = (int)luaL_checkinteger(L, 1);
    int miso = (int)luaL_checkinteger(L, 2);
    int cs   = (int)luaL_checkinteger(L, 3);
    uint8_t mode = (uint8_t)luaL_checkinteger(L, 4);
    size_t len = (size_t)luaL_checkinteger(L, 5);

    if (len > 256) len = 256;
    uint8_t rx[256];

    int ret = hw_spi_read(sck, miso, cs, mode, rx, len);
    if (ret != 0) {
        return luaL_error(L, "hw_spi_read failed: %d", ret);
    }

    lua_newtable(L);
    for (size_t i = 0; i < len; i++) {
        lua_pushinteger(L, rx[i]);
        lua_rawseti(L, -2, (lua_Integer)(i + 1));
    }
    return 1;
}

static int l_spi_status(lua_State *L)
{
    hw_spi_status_t st;
    hw_spi_get_status(&st);

    lua_newtable(L);
    lua_pushinteger(L, st.sck_pin);
    lua_setfield(L, -2, "sck");
    lua_pushinteger(L, st.mosi_pin);
    lua_setfield(L, -2, "mosi");
    lua_pushinteger(L, st.miso_pin);
    lua_setfield(L, -2, "miso");
    lua_pushinteger(L, st.cs_pin);
    lua_setfield(L, -2, "cs");
    lua_pushinteger(L, st.freq_khz);
    lua_setfield(L, -2, "freq_khz");
    lua_pushinteger(L, st.mode);
    lua_setfield(L, -2, "mode");
    lua_pushboolean(L, st.active);
    lua_setfield(L, -2, "active");

    return 1;
}

static const luaL_Reg spi_funcs[] = {
    {"transfer", l_spi_transfer},
    {"write",    l_spi_write},
    {"read",     l_spi_read},
    {"status",   l_spi_status},
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

    lua_pushinteger(L, FSPICLK_OUT_IDX);
    lua_setfield(L, -2, "SPICLK");
    lua_pushinteger(L, FSPICS0_OUT_IDX);
    lua_setfield(L, -2, "SPICS0");
    lua_pushinteger(L, FSPID_OUT_IDX);
    lua_setfield(L, -2, "SPID");
    lua_pushinteger(L, FSPIQ_IN_IDX);
    lua_setfield(L, -2, "SPIQ");

    lua_pushinteger(L, LEDC_LS_SIG_OUT0_IDX);
    lua_setfield(L, -2, "LEDC_OUT0");
    lua_pushinteger(L, LEDC_LS_SIG_OUT1_IDX);
    lua_setfield(L, -2, "LEDC_OUT1");
    lua_pushinteger(L, LEDC_LS_SIG_OUT2_IDX);
    lua_setfield(L, -2, "LEDC_OUT2");
    lua_pushinteger(L, LEDC_LS_SIG_OUT3_IDX);
    lua_setfield(L, -2, "LEDC_OUT3");
    lua_pushinteger(L, LEDC_LS_SIG_OUT4_IDX);
    lua_setfield(L, -2, "LEDC_OUT4");
    lua_pushinteger(L, LEDC_LS_SIG_OUT5_IDX);
    lua_setfield(L, -2, "LEDC_OUT5");
    lua_pushinteger(L, LEDC_LS_SIG_OUT6_IDX);
    lua_setfield(L, -2, "LEDC_OUT6");
    lua_pushinteger(L, LEDC_LS_SIG_OUT7_IDX);
    lua_setfield(L, -2, "LEDC_OUT7");

    lua_setglobal(L, "matrix");

    /* 3. Register 'pwm' */
    luaL_newlib(L, pwm_funcs);
    lua_setglobal(L, "pwm");

    /* 4. Register 'i2c' */
    luaL_newlib(L, i2c_funcs);
    lua_setglobal(L, "i2c");

    /* 5. Register 'spi' */
    luaL_newlib(L, spi_funcs);
    lua_setglobal(L, "spi");

    /* 6. Register 'sys' */
    luaL_newlib(L, sys_funcs);
    lua_setglobal(L, "sys");
}
