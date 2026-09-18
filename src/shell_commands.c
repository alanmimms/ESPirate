/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include <string.h>
#include "shell_commands.h"
#include "lua_manager.h"

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint32_t uptime_ms = (uint32_t)k_uptime_get();
    uint32_t sec = (uptime_ms / 1000) % 60;
    uint32_t min = (uptime_ms / (1000 * 60)) % 60;
    uint32_t hrs = (uptime_ms / (1000 * 60 * 60));
    uint32_t cycles_per_sec = sys_clock_hw_cycles_per_sec();

    shell_print(sh, "=== ESPirate System Info ===");
    shell_print(sh, "Zephyr OS Version : %s", KERNEL_VERSION_STRING);
    shell_print(sh, "CPU Core Clock    : %u MHz", cycles_per_sec / 1000000);
    shell_print(sh, "Uptime            : %u hrs, %02u min, %02u sec (%u ms)",
                hrs, min, sec, uptime_ms);
    shell_print(sh, "Kernel Cycles     : %llu", (unsigned long long)k_cycle_get_64());
    shell_print(sh, "Tick Rate         : %d Hz", CONFIG_SYS_CLOCK_TICKS_PER_SEC);
    shell_print(sh, "Lua VM Memory     : %u KB (256 KB Heap Pool)", (unsigned int)lua_manager_get_memory_kb());
    return 0;
}

static int cmd_ping(const struct shell *sh, size_t argc, char **argv)
{
    if (argc > 1) {
        shell_print(sh, "pong: %s", argv[1]);
    } else {
        shell_print(sh, "pong");
    }
    return 0;
}

static int cmd_echo(const struct shell *sh, size_t argc, char **argv)
{
    for (size_t i = 1; i < argc; i++) {
        shell_fprintf(sh, SHELL_NORMAL, "%s%s", argv[i], (i + 1 < argc) ? " " : "");
    }
    shell_fprintf(sh, SHELL_NORMAL, "\n");
    return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== ESPirate Subsystem Status ===");
    shell_print(sh, "  [*] Phase 1: USB UART Shell  - ACTIVE");
    shell_print(sh, "  [*] Phase 2: Dynamic GPIO    - ACTIVE");
    shell_print(sh, "  [*] Phase 2: Lua Engine      - ACTIVE (%u KB used)",
                (unsigned int)lua_manager_get_memory_kb());
    shell_print(sh, "  [ ] Phase 3: LittleFS        - NOT MOUNTED");
    shell_print(sh, "  [ ] Phase 3: Wi-Fi AP & Web  - NOT STARTED");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_espirate,
    SHELL_CMD(echo, NULL, "Echo back arguments: espirate echo <string>", cmd_echo),
    SHELL_CMD(info, NULL, "Show system & hardware info: espirate info", cmd_info),
    SHELL_CMD(ping, NULL, "Ping-pong test: espirate ping [arg]", cmd_ping),
    SHELL_CMD(status, NULL, "Show subsystem statuses: espirate status", cmd_status),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(espirate, &sub_espirate, "ESPirate commands", NULL);

/* ========================================================================= */
/*                          'lua' SHELL COMMAND                              */
/* ========================================================================= */

static int cmd_lua(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: lua \"<code>\" | lua status | lua reset");
        return -EINVAL;
    }

    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        shell_print(sh, "=== Lua Subsystem Status ===");
        shell_print(sh, "  State      : %s", lua_manager_is_ready() ? "READY" : "NOT INITIALIZED");
        shell_print(sh, "  Allocated  : %u KB", (unsigned int)lua_manager_get_memory_kb());
        shell_print(sh, "  Heap Pool  : %d KB", CONFIG_HEAP_MEM_POOL_SIZE / 1024);
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        shell_print(sh, "Resetting Lua VM...");
        int ret = lua_manager_reset();
        if (ret == 0) {
            shell_print(sh, "Lua VM reset successful.");
        } else {
            shell_error(sh, "Failed to reset Lua VM: %d", ret);
        }
        return ret;
    }

    /* Single string command */
    if (argc == 2) {
        return lua_manager_eval(argv[1], sh);
    }

    /* Combine multiple arguments into a single code buffer */
    char cmd_buf[512] = {0};
    size_t pos = 0;
    for (size_t i = 1; i < argc; i++) {
        size_t len = strlen(argv[i]);
        if (pos + len + 2 >= sizeof(cmd_buf)) {
            shell_error(sh, "Lua command line too long (max %zu bytes)", sizeof(cmd_buf));
            return -E2BIG;
        }
        memcpy(cmd_buf + pos, argv[i], len);
        pos += len;
        if (i + 1 < argc) {
            cmd_buf[pos++] = ' ';
        }
    }
    cmd_buf[pos] = '\0';

    return lua_manager_eval(cmd_buf, sh);
}

SHELL_CMD_REGISTER(lua, NULL, "Execute Lua code: lua \"<code>\" | lua status | lua reset", cmd_lua);

void espirate_shell_init(void)
{
}
