/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include <string.h>
#include <strings.h>
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

#include "fs_manager.h"
#include "lua_worker.h"
#include "wifi_manager.h"
#include "web_server.h"

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== ESPirate Subsystem Status ===");
    shell_print(sh, "  [*] Phase 1: USB UART Shell  - ACTIVE");
    shell_print(sh, "  [*] Phase 2: Dynamic GPIO    - ACTIVE");
    shell_print(sh, "  [*] Phase 2: Lua Engine      - ACTIVE (%u KB used)",
                (unsigned int)lua_manager_get_memory_kb());
    shell_print(sh, "  [*] Phase 3: LittleFS        - %s (/lfs)",
                fs_manager_is_mounted() ? "MOUNTED" : "NOT MOUNTED");
    shell_print(sh, "  [*] Phase 3: Lua Worker      - ACTIVE (%s)",
                lua_worker_is_busy() ? "BUSY" : "IDLE");
    shell_print(sh, "  [*] Wi-Fi Mode              - %s (Configured: %s)",
                wifi_manager_get_mode_str(),
                (wifi_manager_get_configured_mode() == ESPIRATE_WIFI_MODE_STA) ? "STA" : "AP");
    shell_print(sh, "  [*] Soft-AP                 - %s (SSID: %s, %s, clients: %u)",
                wifi_manager_is_ap_active() ? "ACTIVE" : "INACTIVE",
                wifi_manager_get_ap_ssid(),
                wifi_manager_get_ap_ip(),
                wifi_manager_get_station_count());
    bool is_sta_mode = (wifi_manager_get_active_mode() == ESPIRATE_WIFI_MODE_STA);
    if (is_sta_mode) {
        if (wifi_manager_sta_is_connected()) {
            shell_print(sh, "  [*] Wi-Fi Station (STA)      - CONNECTED (SSID: %s, IP: %s)",
                        wifi_manager_get_sta_ssid(), wifi_manager_get_sta_ip());
        } else {
            shell_print(sh, "  [*] Wi-Fi Station (STA)      - CONNECTING (%u/%u)",
                        wifi_manager_get_sta_retry_count(), wifi_manager_get_sta_max_retries());
        }
    } else {
        shell_print(sh, "  [*] Wi-Fi Station (STA)      - DISCONNECTED");
    }
    shell_print(sh, "  [*] Discovery & mDNS         - ACTIVE (http://%s)",
                wifi_manager_get_mdns_domain());
    shell_print(sh, "  [*] Pretend Internet         - %s",
                wifi_manager_get_fake_internet() ? "ENABLED (204 Probe/DNS)" : "DISABLED");
    shell_print(sh, "  [*] Phase 4: Web Server      - %s (port 80, clients: %u)",
                web_server_is_running() ? "ACTIVE" : "INACTIVE",
                wifi_manager_get_station_count());
    return 0;
}

static int cmd_telemetry(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    espirate_telemetry_t t;
    int ret = espirate_telemetry_get(&t);
    if (ret != 0) {
        shell_error(sh, "Failed to read telemetry: %d", ret);
        return ret;
    }

    shell_print(sh, "=== ESPirate Telemetry ===");
    shell_print(sh, "  Total Cycles  : %u", t.total_cycles);
    shell_print(sh, "  Passed Cycles : %u", t.passed_cycles);
    shell_print(sh, "  Failed Cycles : %u", t.failed_cycles);
    shell_print(sh, "  Last Status   : %s", t.last_status);
    return 0;
}

static int cmd_wifi_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== Wi-Fi Subsystem Status ===");
    shell_print(sh, "  Active Mode   : %s", wifi_manager_get_mode_str());
    shell_print(sh, "  Configured    : %s", (wifi_manager_get_configured_mode() == ESPIRATE_WIFI_MODE_STA) ? "STA" : "AP");
    shell_print(sh, "  Soft-AP State : %s", wifi_manager_is_ap_active() ? "BROADCASTING" : "STOPPED");
    shell_print(sh, "  Soft-AP SSID  : %s", wifi_manager_get_ap_ssid());
    shell_print(sh, "  Soft-AP IP    : %s", wifi_manager_get_ap_ip());
    shell_print(sh, "  Connected STAs: %u", wifi_manager_get_station_count());
    bool is_sta = (wifi_manager_get_active_mode() == ESPIRATE_WIFI_MODE_STA);
    if (is_sta) {
        if (wifi_manager_sta_is_connected()) {
            shell_print(sh, "  STA State     : CONNECTED");
        } else {
            shell_print(sh, "  STA State     : CONNECTING (attempt %u/%u)",
                        wifi_manager_get_sta_retry_count(), wifi_manager_get_sta_max_retries());
        }
    } else {
        shell_print(sh, "  STA State     : DISCONNECTED (AP Mode Active)");
    }
    shell_print(sh, "  STA SSID      : %s", wifi_manager_get_sta_ssid());
    shell_print(sh, "  STA IP Address: %s", wifi_manager_get_sta_ip());
    shell_print(sh, "  Credentials   : %s", wifi_manager_has_saved_sta() ? "[CONFIGURED / SECURED]" : "[NONE]");
    shell_print(sh, "  mDNS Hostname : %s (http://%s)",
                wifi_manager_get_hostname(), wifi_manager_get_mdns_domain());
    shell_print(sh, "  Fake Internet : %s",
                wifi_manager_get_fake_internet() ? "ENABLED (Android 204 & Captive DNS)" : "DISABLED");
    shell_print(sh, "  Web Server    : %s (http://%s)",
                web_server_is_running() ? "RUNNING" : "STOPPED",
                wifi_manager_get_ip());
    return 0;
}

static int cmd_wifi_mode(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_print(sh, "Current Wi-Fi mode: %s (Configured: %s)",
                    wifi_manager_get_mode_str(),
                    (wifi_manager_get_configured_mode() == ESPIRATE_WIFI_MODE_STA) ? "STA" : "AP");
        shell_print(sh, "Usage: wifi mode <ap|sta>");
        return 0;
    }

    if (strcasecmp(argv[1], "sta") == 0) {
        if (!wifi_manager_has_saved_sta()) {
            shell_error(sh, "Cannot switch to STA mode: no credentials saved. Use 'wifi sta <ssid> [pass]'.");
            return -ENOENT;
        }
        shell_print(sh, "Switching to Station (STA) mode (connecting to '%s')...", wifi_manager_get_sta_ssid());
        int ret = wifi_manager_set_mode(ESPIRATE_WIFI_MODE_STA, true);
        if (ret != 0) {
            shell_error(sh, "Failed to switch to STA mode: %d", ret);
        }
        return ret;
    } else if (strcasecmp(argv[1], "ap") == 0) {
        shell_print(sh, "Switching to Soft-AP mode ('%s')...", wifi_manager_get_ap_ssid());
        int ret = wifi_manager_set_mode(ESPIRATE_WIFI_MODE_AP, true);
        if (ret != 0) {
            shell_error(sh, "Failed to switch to AP mode: %d", ret);
        }
        return ret;
    } else {
        shell_error(sh, "Invalid mode '%s'. Choose 'ap' or 'sta'.", argv[1]);
        return -EINVAL;
    }
}

static int cmd_wifi_sta(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: wifi sta <ssid> [password]");
        return -EINVAL;
    }

    const char *ssid = argv[1];
    const char *pass = (argc >= 3) ? argv[2] : NULL;

    shell_print(sh, "Saving STA credentials for '%s' and switching to STA mode...", ssid);
    wifi_manager_set_sta_credentials(ssid, pass, true);
    int ret = wifi_manager_set_mode(ESPIRATE_WIFI_MODE_STA, true);
    if (ret == 0) {
        shell_print(sh, "Station mode initiated (up to %u attempts before fallback to AP).",
                    wifi_manager_get_sta_max_retries());
    } else {
        shell_error(sh, "Station connection failed: %d", ret);
    }
    return ret;
}

static int cmd_wifi_ap(const struct shell *sh, size_t argc, char **argv)
{
    if (argc >= 2) {
        const char *ssid = argv[1];
        shell_print(sh, "Setting Soft-AP SSID to '%s'...", ssid);
        wifi_manager_set_ap_ssid(ssid, true);
    }

    shell_print(sh, "Switching to Soft-AP mode ('%s' at %s)...",
                wifi_manager_get_ap_ssid(), wifi_manager_get_ap_ip());
    int ret = wifi_manager_set_mode(ESPIRATE_WIFI_MODE_AP, true);
    if (ret == 0) {
        shell_print(sh, "Soft-AP active. DHCP server running.");
    } else {
        shell_error(sh, "Failed to switch to Soft-AP: %d", ret);
    }
    return ret;
}

static int cmd_wifi_disconnect(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Disconnecting STA and falling back to Soft-AP mode...");
    int ret = wifi_manager_disconnect_sta();
    if (ret == 0) {
        shell_print(sh, "Active mode is now Soft-AP (%s).", wifi_manager_get_ap_ssid());
    } else {
        shell_error(sh, "Disconnect failed: %d", ret);
    }
    return ret;
}

static int cmd_wifi_forget(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Forgetting saved Station credentials and returning to AP mode...");
    wifi_manager_forget_sta();
    shell_print(sh, "Station credentials deleted. Active mode: Soft-AP (%s).", wifi_manager_get_ap_ssid());
    return 0;
}

static int cmd_wifi_fake_internet(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_print(sh, "Pretend Internet Connectivity: %s",
                    wifi_manager_get_fake_internet() ? "ENABLED" : "DISABLED");
        shell_print(sh, "Usage: wifi fake_internet <on|off>");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0 || strcmp(argv[1], "enable") == 0) {
        wifi_manager_set_fake_internet(true);
        shell_print(sh, "Pretend Internet Connectivity ENABLED (204 probe & captive DNS active).");
    } else if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0 || strcmp(argv[1], "disable") == 0) {
        wifi_manager_set_fake_internet(false);
        shell_print(sh, "Pretend Internet Connectivity DISABLED.");
    } else {
        shell_error(sh, "Invalid option. Use 'on' or 'off'.");
        return -EINVAL;
    }
    return 0;
}

static int cmd_wifi_hostname(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "mDNS Hostname: %s", wifi_manager_get_hostname());
    shell_print(sh, "mDNS Domain  : %s", wifi_manager_get_mdns_domain());
    shell_print(sh, "Dashboard URL: http://%s", wifi_manager_get_mdns_domain());
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wifi,
    SHELL_CMD(status, NULL, "Show Wi-Fi & mDNS status: wifi status", cmd_wifi_status),
    SHELL_CMD(mode, NULL, "Get or set Wi-Fi mode: wifi mode <ap|sta>", cmd_wifi_mode),
    SHELL_CMD(sta, NULL, "Configure & connect STA: wifi sta <ssid> [password]", cmd_wifi_sta),
    SHELL_CMD(ap, NULL, "Configure & start Soft-AP: wifi ap [ssid]", cmd_wifi_ap),
    SHELL_CMD(connect, NULL, "Alias for wifi sta: wifi connect <ssid> [password]", cmd_wifi_sta),
    SHELL_CMD(disconnect, NULL, "Disconnect STA (reverts to AP): wifi disconnect", cmd_wifi_disconnect),
    SHELL_CMD(forget, NULL, "Forget saved STA credentials: wifi forget", cmd_wifi_forget),
    SHELL_CMD(fake_internet, NULL, "Toggle pretend internet (204 probe): wifi fake_internet <on|off>", cmd_wifi_fake_internet),
    SHELL_CMD(hostname, NULL, "Show mDNS domain: wifi hostname", cmd_wifi_hostname),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(wifi, &sub_wifi, "Wi-Fi commands", NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_espirate,
    SHELL_CMD(echo, NULL, "Echo back arguments: espirate echo <string>", cmd_echo),
    SHELL_CMD(info, NULL, "Show system & hardware info: espirate info", cmd_info),
    SHELL_CMD(ping, NULL, "Ping-pong test: espirate ping [arg]", cmd_ping),
    SHELL_CMD(status, NULL, "Show subsystem statuses: espirate status", cmd_status),
    SHELL_CMD(telemetry, NULL, "Show hardware test telemetry: espirate telemetry", cmd_telemetry),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(espirate, &sub_espirate, "ESPirate commands", NULL);

/* ========================================================================= */
/*                          'lua' SHELL COMMAND                              */
/* ========================================================================= */

static int cmd_lua(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: lua \"<code>\" | lua run <file> | lua bg <file> | lua status | lua reset");
        return -EINVAL;
    }

    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        shell_print(sh, "=== Lua Subsystem Status ===");
        shell_print(sh, "  VM State   : %s", lua_manager_is_ready() ? "READY" : "NOT INITIALIZED");
        shell_print(sh, "  Worker     : %s", lua_worker_is_busy() ? "BUSY" : "IDLE");
        shell_print(sh, "  Allocated  : %u KB", (unsigned int)lua_manager_get_memory_kb());
        shell_print(sh, "  Heap Pool  : %d KB", CONFIG_HEAP_MEM_POOL_SIZE / 1024);
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        shell_print(sh, "Resetting Lua VM...");
        int ret = lua_worker_reset();
        if (ret == 0) {
            shell_print(sh, "Lua VM reset successful.");
        } else {
            shell_error(sh, "Failed to reset Lua VM: %d", ret);
        }
        return ret;
    }

    if (argc >= 3 && strcmp(argv[1], "run") == 0) {
        return lua_worker_eval_file(argv[2], sh);
    }

    if (argc >= 3 && strcmp(argv[1], "bg") == 0) {
        lua_job_t job = {
            .type = (argv[2][0] == '/') ? LUA_JOB_EVAL_FILE : LUA_JOB_EVAL_STRING,
            .sh = sh,
            .done_sem = NULL,
            .result = 0,
        };
        strncpy(job.payload, argv[2], sizeof(job.payload) - 1);
        job.payload[sizeof(job.payload) - 1] = '\0';
        int ret = lua_worker_submit_async(&job);
        if (ret == 0) {
            shell_print(sh, "[Worker] Job submitted to background worker thread.");
        } else {
            shell_error(sh, "[Worker] Failed to submit job: %d", ret);
        }
        return ret;
    }

    /* Single string command */
    if (argc == 2) {
        return lua_worker_eval(argv[1], sh);
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

    return lua_worker_eval(cmd_buf, sh);
}

SHELL_CMD_REGISTER(lua, NULL, "Execute Lua code: lua \"<code>\" | lua run <file> | lua status | lua reset", cmd_lua);

void espirate_shell_init(void)
{
}
