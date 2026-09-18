/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include "shell_commands.h"
#include "lua_manager.h"
#include "lua_worker.h"
#include "fs_manager.h"
#include "wifi_manager.h"
#include "web_server.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("==================================================");
    LOG_INF("  ESPirate - Hardware Sequencing Architecture");
    LOG_INF("  Phase 4: Wi-Fi AP, Web Server & Remote Sequencing");
    LOG_INF("  Zephyr Kernel: %s", KERNEL_VERSION_STRING);
    LOG_INF("==================================================");

    /* Initialize LittleFS flash storage */
    int ret = fs_manager_init();
    if (ret != 0) {
        LOG_ERR("Failed to initialize LittleFS storage: %d", ret);
    }

    /* Initialize Lua VM and hardware subsystems */
    ret = lua_manager_init();
    if (ret != 0) {
        LOG_ERR("Failed to initialize Lua manager: %d", ret);
    }

    /* Start isolated Lua worker thread */
    ret = lua_worker_init();
    if (ret != 0) {
        LOG_ERR("Failed to initialize Lua worker: %d", ret);
    }

    /* Initialize Wi-Fi AP and DHCP server */
    ret = wifi_manager_init();
    if (ret != 0) {
        LOG_ERR("Failed to initialize Wi-Fi AP: %d", ret);
    }

    /* Start HTTP web server on port 80 */
    ret = web_server_init();
    if (ret != 0) {
        LOG_ERR("Failed to initialize Web server: %d", ret);
    }

    espirate_shell_init();

    LOG_INF("ESPirate shell ready on UART console.");
    LOG_INF("Web Dashboard available at http://%s", wifi_manager_get_ip());
    LOG_INF("Type 'help' or 'lua \"print(\\'hello\\')\"' to begin.");

    while (1) {
        k_sleep(K_SECONDS(30));
        LOG_DBG("Heartbeat tick - system alive (uptime: %lld ms)", k_uptime_get());
    }

    return 0;
}
