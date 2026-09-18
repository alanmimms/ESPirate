/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include "shell_commands.h"
#include "lua_manager.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("==================================================");
    LOG_INF("  ESPirate - Hardware Sequencing Architecture");
    LOG_INF("  Phase 2: Lua Engine & Dynamic GPIO Control");
    LOG_INF("  Zephyr Kernel: %s", KERNEL_VERSION_STRING);
    LOG_INF("==================================================");

    /* Initialize Lua VM and hardware subsystems */
    int ret = lua_manager_init();
    if (ret != 0) {
        LOG_ERR("Failed to initialize Lua manager: %d", ret);
    }

    espirate_shell_init();

    LOG_INF("ESPirate shell ready on UART console.");
    LOG_INF("Type 'help' or 'lua \"print(\\'hello\\')\"' to begin.");

    while (1) {
        k_sleep(K_SECONDS(30));
        LOG_DBG("Heartbeat tick - system alive (uptime: %lld ms)", k_uptime_get());
    }

    return 0;
}
