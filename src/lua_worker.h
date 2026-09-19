/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_LUA_WORKER_H_
#define ESPIRATE_LUA_WORKER_H_

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LUA_JOB_EVAL_STRING,
    LUA_JOB_EVAL_FILE,
    LUA_JOB_RESET,
} lua_job_type_t;

typedef struct {
    lua_job_type_t type;
    char payload[512];
    const struct shell *sh;
    struct k_sem *done_sem;
    int result;
    int *result_out;
} lua_job_t;

/**
 * @brief Initialize the isolated Lua worker thread and message queue.
 * @return 0 on success, negative error code on failure.
 */
int lua_worker_init(void);

/**
 * @brief Submit a code string for evaluation on the Lua worker thread (synchronous).
 * @param code Lua code snippet.
 * @param sh Optional shell pointer for terminal output.
 * @return 0 on success, negative error code on failure.
 */
int lua_worker_eval(const char *code, const struct shell *sh);

/**
 * @brief Submit a script file for evaluation on the Lua worker thread (synchronous).
 * @param path Filesystem path to .lua file (e.g. "/lfs/demo.lua").
 * @param sh Optional shell pointer for terminal output.
 * @return 0 on success, negative error code on failure.
 */
int lua_worker_eval_file(const char *path, const struct shell *sh);

/**
 * @brief Reset the Lua VM via the worker thread.
 * @return 0 on success, negative error code on failure.
 */
int lua_worker_reset(void);

/**
 * @brief Submit a job asynchronously to the worker thread queue without blocking.
 * @param job Pointer to populated job structure.
 * @return 0 on success, negative error code on failure.
 */
int lua_worker_submit_async(const lua_job_t *job);

/**
 * @brief Check if the worker thread is currently busy executing a job.
 * @return true if busy, false if idle.
 */
bool lua_worker_is_busy(void);

/**
 * @brief Wake up the Lua worker thread immediately if it is sleeping.
 */
void lua_worker_interrupt(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_LUA_WORKER_H_ */
