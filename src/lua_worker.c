/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "lua_worker.h"
#include "lua_manager.h"

LOG_MODULE_REGISTER(lua_worker, LOG_LEVEL_INF);

#define LUA_WORKER_STACK_SIZE 16384
#define LUA_WORKER_PRIORITY   7

K_THREAD_STACK_DEFINE(lua_worker_stack, LUA_WORKER_STACK_SIZE);
static struct k_thread lua_worker_thread;
K_MSGQ_DEFINE(lua_worker_msgq, sizeof(lua_job_t), 8, 4);

static atomic_t worker_busy = ATOMIC_INIT(0);
static bool is_initialized = false;

static void lua_worker_thread_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    LOG_INF("Lua worker thread running (stack: %d B, priority: %d)",
            LUA_WORKER_STACK_SIZE, LUA_WORKER_PRIORITY);

    while (1) {
        lua_job_t job;
        int ret = k_msgq_get(&lua_worker_msgq, &job, K_FOREVER);
        if (ret != 0) {
            continue;
        }

        atomic_set(&worker_busy, 1);

        switch (job.type) {
        case LUA_JOB_EVAL_STRING:
            job.result = lua_manager_eval(job.payload, job.sh);
            break;
        case LUA_JOB_EVAL_FILE:
            job.result = lua_manager_eval_file(job.payload, job.sh);
            break;
        case LUA_JOB_RESET:
            job.result = lua_manager_reset();
            break;
        default:
            job.result = -EINVAL;
            break;
        }

        atomic_set(&worker_busy, 0);

        if (job.done_sem) {
            k_sem_give(job.done_sem);
        }
    }
}

int lua_worker_init(void)
{
    if (is_initialized) {
        return 0;
    }

    k_thread_create(&lua_worker_thread,
                    lua_worker_stack,
                    K_THREAD_STACK_SIZEOF(lua_worker_stack),
                    lua_worker_thread_fn,
                    NULL, NULL, NULL,
                    LUA_WORKER_PRIORITY,
                    0,
                    K_NO_WAIT);

    k_thread_name_set(&lua_worker_thread, "lua_worker");
    is_initialized = true;
    return 0;
}

int lua_worker_eval(const char *code, const struct shell *sh)
{
    if (!code) {
        return -EINVAL;
    }

    struct k_sem done_sem;
    k_sem_init(&done_sem, 0, 1);

    lua_job_t job = {
        .type = LUA_JOB_EVAL_STRING,
        .sh = sh,
        .done_sem = &done_sem,
        .result = 0,
    };
    strncpy(job.payload, code, sizeof(job.payload) - 1);
    job.payload[sizeof(job.payload) - 1] = '\0';

    int rc = k_msgq_put(&lua_worker_msgq, &job, K_MSEC(1000));
    if (rc != 0) {
        if (sh) {
            shell_error(sh, "Lua worker queue full");
        }
        return rc;
    }

    k_sem_take(&done_sem, K_FOREVER);
    return job.result;
}

int lua_worker_eval_file(const char *path, const struct shell *sh)
{
    if (!path) {
        return -EINVAL;
    }

    struct k_sem done_sem;
    k_sem_init(&done_sem, 0, 1);

    lua_job_t job = {
        .type = LUA_JOB_EVAL_FILE,
        .sh = sh,
        .done_sem = &done_sem,
        .result = 0,
    };
    strncpy(job.payload, path, sizeof(job.payload) - 1);
    job.payload[sizeof(job.payload) - 1] = '\0';

    int rc = k_msgq_put(&lua_worker_msgq, &job, K_MSEC(1000));
    if (rc != 0) {
        if (sh) {
            shell_error(sh, "Lua worker queue full");
        }
        return rc;
    }

    k_sem_take(&done_sem, K_FOREVER);
    return job.result;
}

int lua_worker_reset(void)
{
    struct k_sem done_sem;
    k_sem_init(&done_sem, 0, 1);

    lua_job_t job = {
        .type = LUA_JOB_RESET,
        .sh = NULL,
        .done_sem = &done_sem,
        .result = 0,
    };

    int rc = k_msgq_put(&lua_worker_msgq, &job, K_MSEC(1000));
    if (rc != 0) {
        return rc;
    }

    k_sem_take(&done_sem, K_FOREVER);
    return job.result;
}

int lua_worker_submit_async(const lua_job_t *job)
{
    if (!job) {
        return -EINVAL;
    }
    return k_msgq_put(&lua_worker_msgq, job, K_NO_WAIT);
}

bool lua_worker_is_busy(void)
{
    return (atomic_get(&worker_busy) != 0);
}
