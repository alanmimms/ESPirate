/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <zephyr/shell/shell.h>

#include "lua_manager.h"
#include "lua_gpio.h"

LOG_MODULE_REGISTER(lua_manager, LOG_LEVEL_INF);

static lua_State *L = NULL;
static const struct shell *eval_shell = NULL;
K_MUTEX_DEFINE(lua_lock);

static int l_zephyr_print(lua_State *L_state)
{
    int n = lua_gettop(L_state);
    char line_buf[512] = {0};
    size_t pos = 0;

    for (int i = 1; i <= n; i++) {
        size_t len;
        const char *s = luaL_tolstring(L_state, i, &len);
        if (i > 1 && pos < sizeof(line_buf) - 1) {
            line_buf[pos++] = '\t';
        }
        if (s && pos + len < sizeof(line_buf) - 1) {
            memcpy(line_buf + pos, s, len);
            pos += len;
        }
        lua_pop(L_state, 1);
    }
    line_buf[pos] = '\0';

    if (eval_shell) {
        shell_print(eval_shell, "%s", line_buf);
    } else {
        printk("%s\n", line_buf);
    }
    return 0;
}

static int lua_panic_handler(lua_State *L_state)
{
    const char *msg = lua_tostring(L_state, -1);
    LOG_ERR("!!! LUA PANIC !!!: %s", msg ? msg : "unknown error");
    return 0;
}

static int lua_init_internal(void)
{
    L = luaL_newstate();
    if (!L) {
        LOG_ERR("Failed to allocate Lua state (out of memory)");
        return -ENOMEM;
    }

    lua_atpanic(L, lua_panic_handler);

    /* Open standard libraries */
    luaL_openlibs(L);

    /* Override print to output via Zephyr printk / console */
    lua_pushcfunction(L, l_zephyr_print);
    lua_setglobal(L, "print");

    /* Register ESPirate hardware modules (gpio, matrix, sys) */
    luaopen_espirate_hardware(L);

    LOG_INF("Lua VM initialized (version: %s, memory: %u KB)",
            LUA_RELEASE, (unsigned int)lua_gc(L, LUA_GCCOUNT, 0));
    return 0;
}

int lua_manager_init(void)
{
    k_mutex_lock(&lua_lock, K_FOREVER);
    espirate_hw_gpio_init();
    int ret = lua_init_internal();
    k_mutex_unlock(&lua_lock);
    return ret;
}

int lua_manager_eval(const char *code, const struct shell *sh)
{
    if (!code) {
        return -EINVAL;
    }

    k_mutex_lock(&lua_lock, K_FOREVER);
    if (!L) {
        k_mutex_unlock(&lua_lock);
        LOG_ERR("Lua VM not initialized");
        return -ENODEV;
    }

    eval_shell = sh;
    int ret = luaL_dostring(L, code);
    if (ret != LUA_OK) {
        const char *err_msg = lua_tostring(L, -1);
        if (eval_shell) {
            shell_error(eval_shell, "Lua Error: %s", err_msg ? err_msg : "unknown error");
        } else {
            printk("Lua Error: %s\n", err_msg ? err_msg : "unknown error");
        }
        lua_pop(L, 1);
        eval_shell = NULL;
        k_mutex_unlock(&lua_lock);
        return -EFAULT;
    }

    eval_shell = NULL;
    k_mutex_unlock(&lua_lock);
    return 0;
}

int lua_manager_reset(void)
{
    k_mutex_lock(&lua_lock, K_FOREVER);
    if (L) {
        lua_close(L);
        L = NULL;
    }
    int ret = lua_init_internal();
    k_mutex_unlock(&lua_lock);
    return ret;
}

size_t lua_manager_get_memory_kb(void)
{
    k_mutex_lock(&lua_lock, K_FOREVER);
    size_t kb = 0;
    if (L) {
        kb = (size_t)lua_gc(L, LUA_GCCOUNT, 0);
    }
    k_mutex_unlock(&lua_lock);
    return kb;
}

bool lua_manager_is_ready(void)
{
    k_mutex_lock(&lua_lock, K_FOREVER);
    bool ready = (L != NULL);
    k_mutex_unlock(&lua_lock);
    return ready;
}
