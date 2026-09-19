/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_LUA_MANAGER_H_
#define ESPIRATE_LUA_MANAGER_H_

#include <stddef.h>
#include <lua.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the global Lua VM state.
 * @return 0 on success, negative on error.
 */
int lua_manager_init(void);

struct shell;

/**
 * @brief Execute a string of Lua code in the persistent VM.
 * @param code Lua code to execute.
 * @param sh Optional pointer to active shell for console output (can be NULL).
 * @return 0 on success, negative on error.
 */
int lua_manager_eval(const char *code, const struct shell *sh);

/**
 * @brief Execute a Lua script file in the persistent VM.
 * @param path Filesystem path to the script (e.g. "/lfs/demo.lua").
 * @param sh Optional pointer to active shell for console output (can be NULL).
 * @return 0 on success, negative on error.
 */
int lua_manager_eval_file(const char *path, const struct shell *sh);

typedef struct {
    uint32_t total_cycles;
    uint32_t passed_cycles;
    uint32_t failed_cycles;
    char last_status[32];
} espirate_telemetry_t;

/**
 * @brief Safely retrieve the current telemetry state.
 * @param out Output pointer.
 * @return 0 on success, negative on error.
 */
int espirate_telemetry_get(espirate_telemetry_t *out);

/**
 * @brief Reset telemetry metrics back to initial zeroes.
 * @return 0 on success.
 */
int espirate_telemetry_reset(void);

/**
 * @brief Reset and recreate the global Lua VM state.
 * @return 0 on success, negative on error.
 */
int lua_manager_reset(void);

/**
 * @brief Get the current memory allocated by the Lua VM in kilobytes.
 * @return Memory usage in KB.
 */
size_t lua_manager_get_memory_kb(void);

/**
 * @brief Check if the Lua VM is currently initialized and ready.
 * @return true if ready, false otherwise.
 */
bool lua_manager_is_ready(void);

/**
 * @brief Request immediate interruption of any running Lua execution.
 * @return 0 on success.
 */
int lua_manager_interrupt(void);

/**
 * @brief Check if execution interruption has been requested.
 * @return true if interrupted, false otherwise.
 */
bool lua_manager_is_interrupted(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_LUA_MANAGER_H_ */
