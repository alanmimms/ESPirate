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
#include "lua_worker.h"
#if CONFIG_SHARED_MULTI_HEAP
#include <zephyr/multi_heap/shared_multi_heap.h>
#endif

LOG_MODULE_REGISTER(lua_manager, LOG_LEVEL_INF);

static lua_State *L = NULL;
static const struct shell *eval_shell = NULL;
K_MUTEX_DEFINE(lua_lock);
static atomic_t s_interrupted = ATOMIC_INIT(0);

static void lua_stop_hook(lua_State *L_state, lua_Debug *ar)
{
    ARG_UNUSED(ar);
    lua_sethook(L_state, NULL, 0, 0);
    luaL_error(L_state, "interrupted by user");
}

bool lua_manager_is_interrupted(void)
{
    return (atomic_get(&s_interrupted) != 0);
}

int lua_manager_interrupt(void)
{
    atomic_set(&s_interrupted, 1);
    if (L) {
        lua_sethook(L, lua_stop_hook, LUA_MASKCOUNT, 1);
    }
    lua_worker_interrupt();
    return 0;
}

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

static espirate_telemetry_t s_telemetry = {
    .total_cycles = 0,
    .passed_cycles = 0,
    .failed_cycles = 0,
    .last_status = "INITIAL",
};
K_MUTEX_DEFINE(s_telemetry_mutex);

int espirate_telemetry_get(espirate_telemetry_t *out)
{
    if (!out) {
        return -EINVAL;
    }
    k_mutex_lock(&s_telemetry_mutex, K_FOREVER);
    memcpy(out, &s_telemetry, sizeof(espirate_telemetry_t));
    k_mutex_unlock(&s_telemetry_mutex);
    return 0;
}

int espirate_telemetry_reset(void)
{
    k_mutex_lock(&s_telemetry_mutex, K_FOREVER);
    s_telemetry.total_cycles = 0;
    s_telemetry.passed_cycles = 0;
    s_telemetry.failed_cycles = 0;
    strncpy(s_telemetry.last_status, "IDLE", sizeof(s_telemetry.last_status) - 1);
    s_telemetry.last_status[sizeof(s_telemetry.last_status) - 1] = '\0';
    k_mutex_unlock(&s_telemetry_mutex);
    return 0;
}

static int l_telemetry_set(lua_State *L_state)
{
    const char *key = luaL_checkstring(L_state, 1);
    k_mutex_lock(&s_telemetry_mutex, K_FOREVER);
    if (strcmp(key, "status") == 0) {
        const char *val = luaL_checkstring(L_state, 2);
        strncpy(s_telemetry.last_status, val, sizeof(s_telemetry.last_status) - 1);
        s_telemetry.last_status[sizeof(s_telemetry.last_status) - 1] = '\0';
    } else if (strcmp(key, "total") == 0) {
        s_telemetry.total_cycles = (uint32_t)luaL_checkinteger(L_state, 2);
    } else if (strcmp(key, "pass") == 0) {
        s_telemetry.passed_cycles = (uint32_t)luaL_checkinteger(L_state, 2);
    } else if (strcmp(key, "fail") == 0) {
        s_telemetry.failed_cycles = (uint32_t)luaL_checkinteger(L_state, 2);
    }
    k_mutex_unlock(&s_telemetry_mutex);
    return 0;
}

static int l_telemetry_inc(lua_State *L_state)
{
    const char *key = luaL_checkstring(L_state, 1);
    uint32_t delta = (uint32_t)luaL_optinteger(L_state, 2, 1);

    k_mutex_lock(&s_telemetry_mutex, K_FOREVER);
    if (strcmp(key, "total") == 0 || strcmp(key, "cycle") == 0 || strcmp(key, "cycles") == 0) {
        s_telemetry.total_cycles += delta;
    } else if (strcmp(key, "pass") == 0) {
        s_telemetry.passed_cycles += delta;
        s_telemetry.total_cycles += delta;
    } else if (strcmp(key, "fail") == 0) {
        s_telemetry.failed_cycles += delta;
        s_telemetry.total_cycles += delta;
    }
    k_mutex_unlock(&s_telemetry_mutex);
    return 0;
}

static int l_telemetry_get(lua_State *L_state)
{
    const char *key = luaL_checkstring(L_state, 1);
    k_mutex_lock(&s_telemetry_mutex, K_FOREVER);
    if (strcmp(key, "status") == 0) {
        lua_pushstring(L_state, s_telemetry.last_status);
    } else if (strcmp(key, "total") == 0 || strcmp(key, "cycles") == 0) {
        lua_pushinteger(L_state, s_telemetry.total_cycles);
    } else if (strcmp(key, "pass") == 0) {
        lua_pushinteger(L_state, s_telemetry.passed_cycles);
    } else if (strcmp(key, "fail") == 0) {
        lua_pushinteger(L_state, s_telemetry.failed_cycles);
    } else {
        lua_pushnil(L_state);
    }
    k_mutex_unlock(&s_telemetry_mutex);
    return 1;
}

static const luaL_Reg telemetry_funcs[] = {
    {"set", l_telemetry_set},
    {"inc", l_telemetry_inc},
    {"get", l_telemetry_get},
    {NULL, NULL}
};

static void luaopen_telemetry(lua_State *L_state)
{
    luaL_newlib(L_state, telemetry_funcs);
    lua_setglobal(L_state, "telemetry");
}

#if CONFIG_SHARED_MULTI_HEAP
static void *lua_psram_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    ARG_UNUSED(ud);
    ARG_UNUSED(osize);

    if (nsize == 0) {
        if (ptr != NULL) {
            shared_multi_heap_free(ptr);
        }
        return NULL;
    }

    if (ptr == NULL) {
        return shared_multi_heap_alloc(SMH_REG_ATTR_EXTERNAL, nsize);
    }

    return shared_multi_heap_realloc(SMH_REG_ATTR_EXTERNAL, ptr, nsize);
}
#endif

static int lua_init_internal(void)
{
#if CONFIG_SHARED_MULTI_HEAP
    /* Probe PSRAM external memory before allocating Lua state */
    void *psram_test = shared_multi_heap_alloc(SMH_REG_ATTR_EXTERNAL, 64);
    if (psram_test) {
        shared_multi_heap_free(psram_test);
        L = lua_newstate(lua_psram_alloc, NULL, luaL_makeseed(NULL));
        if (L) {
            LOG_INF("Lua VM memory pool allocated on 8MB Octal PSRAM");
        }
    }
#endif

    if (!L) {
        L = luaL_newstate();
        if (!L) {
            LOG_ERR("Failed to allocate Lua state (out of memory)");
            return -ENOMEM;
        }
        LOG_INF("Lua VM memory pool allocated on internal SRAM");
    }

    lua_atpanic(L, lua_panic_handler);

    /* Open standard libraries */
    luaL_openlibs(L);

    /* Override print to output via Zephyr printk / console */
    lua_pushcfunction(L, l_zephyr_print);
    lua_setglobal(L, "print");

    /* Register ESPirate hardware modules (gpio, matrix, sys) */
    luaopen_espirate_hardware(L);

    /* Register telemetry module */
    luaopen_telemetry(L);

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

    atomic_set(&s_interrupted, 0);
    lua_sethook(L, NULL, 0, 0);

    eval_shell = sh;
    int ret = luaL_dostring(L, code);
    lua_sethook(L, NULL, 0, 0);

    if (ret != LUA_OK) {
        bool was_interrupted = lua_manager_is_interrupted();
        const char *err_msg = lua_tostring(L, -1);
        if (eval_shell) {
            shell_error(eval_shell, "Lua Error: %s", err_msg ? err_msg : "unknown error");
        } else {
            printk("Lua Error: %s\n", err_msg ? err_msg : "unknown error");
        }
        if (was_interrupted) {
            k_mutex_lock(&s_telemetry_mutex, K_FOREVER);
            strncpy(s_telemetry.last_status, "INTERRUPTED", sizeof(s_telemetry.last_status) - 1);
            s_telemetry.last_status[sizeof(s_telemetry.last_status) - 1] = '\0';
            k_mutex_unlock(&s_telemetry_mutex);
        }
        lua_pop(L, 1);
        eval_shell = NULL;
        k_mutex_unlock(&lua_lock);
        return was_interrupted ? -EINTR : -EFAULT;
    }

    eval_shell = NULL;
    k_mutex_unlock(&lua_lock);
    return 0;
}

int lua_manager_eval_file(const char *path, const struct shell *sh)
{
    if (!path) {
        return -EINVAL;
    }

    k_mutex_lock(&lua_lock, K_FOREVER);
    if (!L) {
        k_mutex_unlock(&lua_lock);
        LOG_ERR("Lua VM not initialized");
        return -ENODEV;
    }

    atomic_set(&s_interrupted, 0);
    lua_sethook(L, NULL, 0, 0);

    eval_shell = sh;
    int ret = luaL_dofile(L, path);
    lua_sethook(L, NULL, 0, 0);

    if (ret != LUA_OK) {
        bool was_interrupted = lua_manager_is_interrupted();
        const char *err_msg = lua_tostring(L, -1);
        if (eval_shell) {
            shell_error(eval_shell, "Lua Error [%s]: %s", path, err_msg ? err_msg : "unknown error");
        } else {
            printk("Lua Error [%s]: %s\n", path, err_msg ? err_msg : "unknown error");
        }
        if (was_interrupted) {
            k_mutex_lock(&s_telemetry_mutex, K_FOREVER);
            strncpy(s_telemetry.last_status, "INTERRUPTED", sizeof(s_telemetry.last_status) - 1);
            s_telemetry.last_status[sizeof(s_telemetry.last_status) - 1] = '\0';
            k_mutex_unlock(&s_telemetry_mutex);
        }
        lua_pop(L, 1);
        eval_shell = NULL;
        k_mutex_unlock(&lua_lock);
        return was_interrupted ? -EINTR : -EFAULT;
    }

    eval_shell = NULL;
    k_mutex_unlock(&lua_lock);
    return 0;
}

int lua_manager_reset(void)
{
    k_mutex_lock(&lua_lock, K_FOREVER);
    atomic_set(&s_interrupted, 0);
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
    static size_t last_kb = 0;
    if (k_mutex_lock(&lua_lock, K_MSEC(10)) == 0) {
        if (L) {
            last_kb = (size_t)lua_gc(L, LUA_GCCOUNT, 0);
        }
        k_mutex_unlock(&lua_lock);
    }
    return last_kb;
}

bool lua_manager_is_ready(void)
{
    return (L != NULL);
}
