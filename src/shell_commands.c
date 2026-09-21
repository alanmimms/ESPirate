/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <soc/gpio_sig_map.h>
#include "shell_commands.h"
#include "lua_manager.h"
#include "hw_gpio.h"
#include "hw_pwm.h"
#include "hw_i2c.h"
#include "hw_spi.h"

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
    shell_print(sh, "  STA Security  : %s", wifi_manager_get_sta_security_str());
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

static int cmd_wifi_scan(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Scanning 2.4 GHz Wi-Fi channels... (AP results will appear in log)");
    int ret = wifi_manager_scan();
    if (ret != 0) {
        shell_error(sh, "Failed to start Wi-Fi scan: %d", ret);
        return ret;
    }
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
        shell_error(sh, "Usage: wifi sta <ssid> [password] [wpa2|wpa3|open]");
        return -EINVAL;
    }

    const char *ssid = argv[1];
    const char *pass = (argc >= 3) ? argv[2] : NULL;
    const char *sec  = (argc >= 4) ? argv[3] : "wpa2";

    shell_print(sh, "Saving STA credentials: SSID='%s', Security='%s', switching to STA mode...", ssid, sec);
    wifi_manager_set_sta_credentials(ssid, pass, sec, true);
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
    SHELL_CMD(scan, NULL, "Scan 2.4 GHz Wi-Fi networks: wifi scan", cmd_wifi_scan),
    SHELL_CMD(mode, NULL, "Get or set Wi-Fi mode: wifi mode <ap|sta>", cmd_wifi_mode),
    SHELL_CMD(sta, NULL, "Configure & connect STA: wifi sta <ssid> [password] [wpa2|wpa3|open]", cmd_wifi_sta),
    SHELL_CMD(ap, NULL, "Configure & start Soft-AP: wifi ap [ssid]", cmd_wifi_ap),
    SHELL_CMD(connect, NULL, "Alias for wifi sta: wifi connect <ssid> [password] [wpa2|wpa3|open]", cmd_wifi_sta),
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
/*                       'storage' SHELL COMMANDS                            */
/* ========================================================================= */

#include <zephyr/fs/fs.h>
#include <esp_flash.h>

static int cmd_storage_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!fs_manager_is_mounted()) {
        shell_print(sh, "LittleFS status: UNMOUNTED");
        return 0;
    }

    size_t total = 0, free_b = 0;
    int rc = fs_manager_statvfs(&total, &free_b);
    if (rc != 0) {
        shell_error(sh, "Failed to get LittleFS filesystem stats: %d", rc);
        return rc;
    }

    uint32_t chip_size = fs_manager_get_chip_size();
    if (chip_size == 0) {
        esp_flash_get_physical_size(NULL, &chip_size);
    }

    size_t used = (total >= free_b) ? (total - free_b) : 0;
    shell_print(sh, "=== LittleFS Storage Subsystem ===");
    shell_print(sh, "  Mount Point : %s", ESPIRATE_FS_MOUNT_POINT);
    shell_print(sh, "  State       : MOUNTED");
    shell_print(sh, "  Total Space : %zu KB (%zu bytes)", total / 1024, total);
    shell_print(sh, "  Used Space  : %zu KB (%zu bytes)", used / 1024, used);
    shell_print(sh, "  Free Space  : %zu KB (%zu bytes)", free_b / 1024, free_b);
    if (chip_size > 0) {
        shell_print(sh, "  Flash Chip  : %u MB physical SPI Flash", chip_size / (1024 * 1024));
        shell_print(sh, "  Partition   : Dynamic (0x200000 -> 0x%08X)", chip_size);
    } else {
        shell_print(sh, "  Partition   : 2 MB baseline at offset 0x200000");
    }
    return 0;
}

static int cmd_storage_ls(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!fs_manager_is_mounted()) {
        shell_error(sh, "LittleFS is not mounted");
        return -ENODEV;
    }

    struct fs_dir_t dir;
    fs_dir_t_init(&dir);

    int rc = fs_opendir(&dir, ESPIRATE_FS_MOUNT_POINT);
    if (rc != 0) {
        shell_error(sh, "Failed to open directory %s: %d", ESPIRATE_FS_MOUNT_POINT, rc);
        return rc;
    }

    shell_print(sh, "Files in %s:", ESPIRATE_FS_MOUNT_POINT);
    shell_print(sh, "  %-24s %10s", "Name", "Size (bytes)");
    shell_print(sh, "  %-24s %10s", "------------------------", "----------");

    struct fs_dirent entry;
    size_t count = 0;
    size_t total_size = 0;

    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_FILE) {
            shell_print(sh, "  %-24s %10zu", entry.name, entry.size);
            count++;
            total_size += entry.size;
        }
    }
    fs_closedir(&dir);

    shell_print(sh, "  %-24s %10s", "------------------------", "----------");
    shell_print(sh, "  Total: %zu file(s), %zu bytes", count, total_size);
    return 0;
}

static int cmd_storage_cat(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: storage cat <filename>");
        return -EINVAL;
    }

    static char buf[2048];
    size_t bytes_read = 0;
    int rc = fs_manager_read_file(argv[1], buf, sizeof(buf) - 1, &bytes_read);
    if (rc != 0) {
        shell_error(sh, "Failed to read file '%s': %d", argv[1], rc);
        return rc;
    }

    buf[bytes_read] = '\0';
    shell_print(sh, "%s", buf);
    return 0;
}

static int cmd_storage_rm(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: storage rm <filename>");
        return -EINVAL;
    }

    int rc = fs_manager_delete_file(argv[1]);
    if (rc == 0) {
        shell_print(sh, "Deleted file: %s", argv[1]);
    } else {
        shell_error(sh, "Failed to delete file '%s': %d", argv[1], rc);
    }
    return rc;
}

static int cmd_storage_rename(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: storage rename <old_name> <new_name>");
        return -EINVAL;
    }

    int rc = fs_manager_rename_file(argv[1], argv[2]);
    if (rc == 0) {
        shell_print(sh, "Renamed '%s' to '%s'", argv[1], argv[2]);
    } else {
        shell_error(sh, "Failed to rename '%s' to '%s': %d", argv[1], argv[2], rc);
    }
    return rc;
}

static int cmd_storage_format(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Formatting LittleFS partition...");
    int rc = fs_manager_format();
    if (rc == 0) {
        shell_print(sh, "LittleFS successfully formatted and remounted at %s.", ESPIRATE_FS_MOUNT_POINT);
    } else {
        shell_error(sh, "Failed to format LittleFS: %d", rc);
    }
    return rc;
}

static int cmd_storage_restore_docs(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Restoring /lfs/howto.md from firmware documentation image...");
    int rc = fs_manager_restore_docs();
    if (rc == 0) {
        shell_print(sh, "Successfully restored /lfs/howto.md.");
    } else {
        shell_error(sh, "Failed to restore /lfs/howto.md: %d", rc);
    }
    return rc;
}

static int cmd_storage_restore_web(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Restoring /lfs/index.html from firmware dashboard image...");
    int rc = fs_manager_restore_web();
    if (rc == 0) {
        shell_print(sh, "Successfully restored /lfs/index.html.");
    } else {
        shell_error(sh, "Failed to restore /lfs/index.html: %d", rc);
    }
    return rc;
}

static int cmd_storage_restore_favicon(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Restoring /lfs/favicon.png from firmware favicon image...");
    int rc = fs_manager_restore_favicon();
    if (rc == 0) {
        shell_print(sh, "Successfully restored /lfs/favicon.png.");
    } else {
        shell_error(sh, "Failed to restore /lfs/favicon.png: %d", rc);
    }
    return rc;
}

static int cmd_storage_restore_all(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Restoring all default files (demo.lua, howto.md, index.html, favicon.png) from firmware...");
    int rc = fs_manager_restore_all();
    if (rc == 0) {
        shell_print(sh, "Successfully restored all default files.");
    } else {
        shell_error(sh, "Failed to restore all files: %d", rc);
    }
    return rc;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_storage,
    SHELL_CMD(status, NULL, "Show storage statistics: storage status", cmd_storage_status),
    SHELL_CMD(ls, NULL, "List files in LittleFS: storage ls", cmd_storage_ls),
    SHELL_CMD(cat, NULL, "Print file contents: storage cat <filename>", cmd_storage_cat),
    SHELL_CMD(rm, NULL, "Delete file: storage rm <filename>", cmd_storage_rm),
    SHELL_CMD(rename, NULL, "Rename file: storage rename <old> <new>", cmd_storage_rename),
    SHELL_CMD(format, NULL, "Reformat LittleFS: storage format", cmd_storage_format),
    SHELL_CMD(restore_docs, NULL, "Restore howto.md from firmware: storage restore_docs", cmd_storage_restore_docs),
    SHELL_CMD(restore_web, NULL, "Restore index.html from firmware: storage restore_web", cmd_storage_restore_web),
    SHELL_CMD(restore_favicon, NULL, "Restore favicon.png from firmware: storage restore_favicon", cmd_storage_restore_favicon),
    SHELL_CMD(restore_all, NULL, "Restore all default files from firmware: storage restore_all", cmd_storage_restore_all),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(storage, &sub_storage, "LittleFS storage commands", NULL);

/* ========================================================================= */
/*                       'gpio' SHELL COMMANDS                               */
/* ========================================================================= */

static int cmd_gpio_status(const struct shell *sh, size_t argc, char **argv)
{
    int pin = -1;
    if (argc >= 2 && strcmp(argv[1], "status") != 0) {
        pin = atoi(argv[1]);
    } else if (argc >= 3) {
        pin = atoi(argv[2]);
    }

    if (pin >= 0) {
        char mode_buf[64] = {0};
        int level = -1;
        int ret = hw_gpio_get_state(pin, mode_buf, sizeof(mode_buf), &level);
        if (ret != 0) {
            shell_error(sh, "GPIO %d: invalid or reserved", pin);
            return ret;
        }
        shell_print(sh, "GPIO %2d : %-32s | Level: %d", pin, mode_buf, level);
        return 0;
    }

    shell_print(sh, "=== ESP32-S3 GPIO State Table ===");
    shell_print(sh, "Pin  State & Configuration               Level  Notes");
    shell_print(sh, "---  ----------------------------------  -----  ---------------------------");
    for (int p = 0; p <= 48; p++) {
        if (hw_gpio_check_safety(p, NULL) != 0) {
            continue;
        }
        char mode_buf[64] = {0};
        int level = -1;
        hw_gpio_get_state(p, mode_buf, sizeof(mode_buf), &level);
        hw_gpio_info_t info;
        hw_gpio_get_info(p, &info);
        shell_print(sh, "%2d   %-34s    %d    %s",
                    p, mode_buf, level, info.desc ? info.desc : "");
    }
    return 0;
}

static int cmd_gpio_list(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== Available ESP32-S3 Safe GPIO Pins ===");
    shell_print(sh, "Pin  Status    Analog / ADC    Notes");
    shell_print(sh, "---  --------  --------------  ---------------------------------");
    for (int p = 0; p <= 48; p++) {
        hw_gpio_info_t info;
        if (hw_gpio_get_info(p, &info) != 0) continue;
        if (info.reserved) {
            shell_print(sh, "%2d   RESERVED  %-14s  %s", p, info.adc_name ? info.adc_name : "-", info.desc);
        } else {
            shell_print(sh, "%2d   USABLE    %-14s  %s", p, info.adc_name ? info.adc_name : "-", info.desc);
        }
    }
    return 0;
}

static int cmd_gpio_info(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: gpio info <pin>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    hw_gpio_info_t info;
    int ret = hw_gpio_get_info(pin, &info);
    if (ret != 0) {
        shell_error(sh, "Pin %d out of range (0..48)", pin);
        return ret;
    }

    shell_print(sh, "=== GPIO Pin %d Metadata ===", pin);
    shell_print(sh, "  Bonded Silicon Pad : %s", info.valid ? "YES" : "NO");
    shell_print(sh, "  System Reserved    : %s", info.reserved ? "YES (Protected)" : "NO (Safe)");
    shell_print(sh, "  Digital Input      : %s", info.input ? "SUPPORTED" : "NO");
    shell_print(sh, "  Digital Output     : %s", info.output ? "SUPPORTED" : "NO");
    shell_print(sh, "  Pull-up/Pull-down  : %s", (info.pullup && info.pulldown) ? "SUPPORTED" : "NO");
    shell_print(sh, "  Analog ADC Input   : %s", info.analog ? info.adc_name : "NO");
    shell_print(sh, "  Description        : %s", info.desc ? info.desc : "");

    char mode_buf[64] = {0};
    int level = -1;
    if (hw_gpio_get_state(pin, mode_buf, sizeof(mode_buf), &level) == 0) {
        shell_print(sh, "  Current Config     : %s", mode_buf);
        shell_print(sh, "  Current Logic Level: %d", level);
    }
    return 0;
}

static int cmd_gpio_mode(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: gpio mode <pin> <in|out|open_drain|tristate> [up|down|none]");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    const char *mode = argv[2];
    const char *pull = (argc > 3) ? argv[3] : NULL;

    int ret = hw_gpio_mode(pin, mode, pull);
    if (ret != 0) {
        shell_error(sh, "Failed to set GPIO %d mode to '%s': %d", pin, mode, ret);
        return ret;
    }
    shell_print(sh, "GPIO %d configured as %s%s%s", pin, mode, pull ? " with pull " : "", pull ? pull : "");
    return 0;
}

static int cmd_gpio_read(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: gpio read <pin>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int val = hw_gpio_read(pin);
    if (val < 0) {
        shell_error(sh, "Failed to read GPIO %d: %d", pin, val);
        return val;
    }
    shell_print(sh, "GPIO %d = %d (%s)", pin, val, val ? "HIGH" : "LOW");
    return 0;
}

static int cmd_gpio_write(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: gpio write <pin> <0|1>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int val = atoi(argv[2]);
    int ret = hw_gpio_write(pin, val);
    if (ret != 0) {
        shell_error(sh, "Failed to write GPIO %d: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "GPIO %d set to %d (%s)", pin, val ? 1 : 0, val ? "HIGH" : "LOW");
    return 0;
}

static int cmd_gpio_high(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: gpio high <pin>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int ret = hw_gpio_high(pin);
    if (ret != 0) {
        shell_error(sh, "Failed to set GPIO %d HIGH: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "GPIO %d set to 1 (HIGH)", pin);
    return 0;
}

static int cmd_gpio_low(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: gpio low <pin>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int ret = hw_gpio_low(pin);
    if (ret != 0) {
        shell_error(sh, "Failed to set GPIO %d LOW: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "GPIO %d set to 0 (LOW)", pin);
    return 0;
}

static int cmd_gpio_toggle(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: gpio toggle <pin>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int ret = hw_gpio_toggle(pin);
    if (ret != 0) {
        shell_error(sh, "Failed to toggle GPIO %d: %d", pin, ret);
        return ret;
    }
    int val = hw_gpio_read(pin);
    shell_print(sh, "GPIO %d toggled -> %d (%s)", pin, val, val ? "HIGH" : "LOW");
    return 0;
}

static int cmd_gpio_tristate(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: gpio tristate <pin>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int ret = hw_gpio_tristate(pin);
    if (ret != 0) {
        shell_error(sh, "Failed to tristate GPIO %d: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "GPIO %d set to High-Impedance / Tristate (Hi-Z)", pin);
    return 0;
}

static int cmd_gpio_pull(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: gpio pull <pin> <up|down|none>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int ret = hw_gpio_pull(pin, argv[2]);
    if (ret != 0) {
        shell_error(sh, "Failed to set GPIO %d pull: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "GPIO %d pull configured to '%s'", pin, argv[2]);
    return 0;
}

static int cmd_gpio_drive(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: gpio drive <pin> <5|10|20|40>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int ma = atoi(argv[2]);
    int ret = hw_gpio_drive(pin, ma);
    if (ret != 0) {
        shell_error(sh, "Failed to set GPIO %d drive strength: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "GPIO %d drive strength set to %d mA", pin, ma);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_gpio,
    SHELL_CMD(status, NULL, "Show GPIO pin status: gpio status [pin]", cmd_gpio_status),
    SHELL_CMD(list, NULL, "List safe ESP32-S3 GPIO pins: gpio list", cmd_gpio_list),
    SHELL_CMD(info, NULL, "Show pin hardware capabilities: gpio info <pin>", cmd_gpio_info),
    SHELL_CMD(mode, NULL, "Set pin mode: gpio mode <pin> <in|out|open_drain|tristate> [up|down|none]", cmd_gpio_mode),
    SHELL_CMD(read, NULL, "Read digital level: gpio read <pin>", cmd_gpio_read),
    SHELL_CMD(write, NULL, "Write digital level: gpio write <pin> <0|1>", cmd_gpio_write),
    SHELL_CMD(high, NULL, "Set pin HIGH (1): gpio high <pin>", cmd_gpio_high),
    SHELL_CMD(low, NULL, "Set pin LOW (0): gpio low <pin>", cmd_gpio_low),
    SHELL_CMD(toggle, NULL, "Toggle digital output: gpio toggle <pin>", cmd_gpio_toggle),
    SHELL_CMD(tristate, NULL, "Disconnect pin (Hi-Z): gpio tristate <pin>", cmd_gpio_tristate),
    SHELL_CMD(hiz, NULL, "Alias for tristate: gpio hiz <pin>", cmd_gpio_tristate),
    SHELL_CMD(pull, NULL, "Configure pull resistor: gpio pull <pin> <up|down|none>", cmd_gpio_pull),
    SHELL_CMD(drive, NULL, "Set drive strength: gpio drive <pin> <5|10|20|40>", cmd_gpio_drive),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(gpio, &sub_gpio, "ESPirate GPIO commands (status, mode, read, write, high, low, toggle, tristate)", cmd_gpio_status);

/* ========================================================================= */
/*                      'matrix' SHELL COMMANDS                              */
/* ========================================================================= */

static int cmd_matrix_status(const struct shell *sh, size_t argc, char **argv)
{
    int pin = -1;
    if (argc >= 2 && strcmp(argv[1], "status") != 0) {
        pin = atoi(argv[1]);
    } else if (argc >= 3) {
        pin = atoi(argv[2]);
    }

    if (pin >= 0) {
        char mode_buf[64] = {0};
        int level = -1;
        if (hw_gpio_get_state(pin, mode_buf, sizeof(mode_buf), &level) != 0) {
            shell_error(sh, "GPIO %d invalid or reserved", pin);
            return -EINVAL;
        }
        shell_print(sh, "GPIO %2d : %s", pin, mode_buf);
        return 0;
    }

    shell_print(sh, "=== GPIO Matrix Routing Table ===");
    shell_print(sh, "Pin  Configured Route & Signal");
    shell_print(sh, "---  --------------------------------------------------");
    int count = 0;
    for (int p = 0; p <= 48; p++) {
        if (hw_gpio_check_safety(p, NULL) != 0) continue;
        char mode_buf[64] = {0};
        int level = -1;
        hw_gpio_get_state(p, mode_buf, sizeof(mode_buf), &level);
        if (strstr(mode_buf, "Matrix") != NULL) {
            shell_print(sh, "%2d   %s", p, mode_buf);
            count++;
        }
    }
    if (count == 0) {
        shell_print(sh, "(No peripheral signals currently routed via GPIO matrix)");
    }
    return 0;
}

static int cmd_matrix_list(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(sh, "=== Common ESP32-S3 Output Signals ===");
    shell_print(sh, "Signal ID  Name / Peripheral");
    shell_print(sh, "---------  ----------------------------------------");
    shell_print(sh, " %3d       GPIO_OUT (Default GPIO output)", SIG_GPIO_OUT_IDX);
    shell_print(sh, " %3d       U0TXD (UART0 Console TX)", U0TXD_OUT_IDX);
    shell_print(sh, " %3d       U1TXD (UART1 TX)", U1TXD_OUT_IDX);
    shell_print(sh, " %3d       U2TXD (UART2 TX)", U2TXD_OUT_IDX);
    shell_print(sh, " %3d       I2C0_SCL (I2C0 Clock)", I2CEXT0_SCL_OUT_IDX);
    shell_print(sh, " %3d       I2C0_SDA (I2C0 Data)", I2CEXT0_SDA_OUT_IDX);
    shell_print(sh, " %3d       I2C1_SCL (I2C1 Clock)", I2CEXT1_SCL_OUT_IDX);
    shell_print(sh, " %3d       I2C1_SDA (I2C1 Data)", I2CEXT1_SDA_OUT_IDX);
    shell_print(sh, " %3d       SPICLK (SPI2 Clock)", FSPICLK_OUT_IDX);
    shell_print(sh, " %3d       SPID (SPI2 MOSI)", FSPID_OUT_IDX);
    shell_print(sh, " %3d       SPICS0 (SPI2 CS0)", FSPICS0_OUT_IDX);
    shell_print(sh, " %3d..%3d  LEDC_OUT0..7 (LEDC Hardware PWM Channels 0..7)",
                LEDC_LS_SIG_OUT0_IDX, LEDC_LS_SIG_OUT7_IDX);
    return 0;
}

static int cmd_matrix_route_out(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: matrix route_out <pin> <sig_idx> [inv_out] [inv_oen]");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int sig = atoi(argv[2]);
    bool inv_out = (argc > 3) ? (atoi(argv[3]) != 0) : false;
    bool inv_oen = (argc > 4) ? (atoi(argv[4]) != 0) : false;

    int ret = hw_matrix_route_out(pin, sig, inv_out, inv_oen);
    if (ret != 0) {
        shell_error(sh, "Failed to route signal %d to GPIO %d: %d", sig, pin, ret);
        return ret;
    }
    shell_print(sh, "Routed Signal %d (%s) to GPIO %d", sig, hw_matrix_get_signal_name(sig), pin);
    return 0;
}

static int cmd_matrix_route_in(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: matrix route_in <pin> <sig_idx> [invert]");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int sig = atoi(argv[2]);
    bool inv = (argc > 3) ? (atoi(argv[3]) != 0) : false;

    int ret = hw_matrix_route_in(pin, sig, inv);
    if (ret != 0) {
        shell_error(sh, "Failed to route GPIO %d to signal %d: %d", pin, sig, ret);
        return ret;
    }
    shell_print(sh, "Routed GPIO %d into Signal %d", pin, sig);
    return 0;
}

static int cmd_matrix_detach(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: matrix detach <pin>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int ret = hw_matrix_detach(pin);
    if (ret != 0) {
        shell_error(sh, "Failed to detach GPIO %d: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "GPIO %d detached from matrix (restored to standard GPIO)", pin);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_matrix,
    SHELL_CMD(status, NULL, "Show GPIO Matrix routing table: matrix status [pin]", cmd_matrix_status),
    SHELL_CMD(list, NULL, "List available peripheral signals: matrix list", cmd_matrix_list),
    SHELL_CMD(route_out, NULL, "Route peripheral signal out: matrix route_out <pin> <sig_idx> [inv_out] [inv_oen]", cmd_matrix_route_out),
    SHELL_CMD(route_in, NULL, "Route pin into peripheral input: matrix route_in <pin> <sig_idx> [inv]", cmd_matrix_route_in),
    SHELL_CMD(detach, NULL, "Detach matrix route: matrix detach <pin>", cmd_matrix_detach),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(matrix, &sub_matrix, "ESPirate GPIO Matrix commands", cmd_matrix_status);

/* ========================================================================= */
/*                        'pwm' SHELL COMMANDS                               */
/* ========================================================================= */

static int cmd_pwm_status(const struct shell *sh, size_t argc, char **argv)
{
    int pin = -1;
    if (argc >= 2 && strcmp(argv[1], "status") != 0) {
        pin = atoi(argv[1]);
    } else if (argc >= 3) {
        pin = atoi(argv[2]);
    }

    if (pin >= 0) {
        hw_pwm_status_t st;
        int ret = hw_pwm_get_pin_status(pin, &st);
        if (ret != 0) {
            shell_print(sh, "GPIO %d: PWM inactive", pin);
            return 0;
        }
        shell_print(sh, "GPIO %2d : PWM Channel %d | %u Hz | Duty: %u%%",
                    pin, st.channel, st.freq_hz, st.duty_percent);
        return 0;
    }

    hw_pwm_status_t list[HW_PWM_MAX_CHANNELS];
    size_t count = 0;
    hw_pwm_get_all_status(list, HW_PWM_MAX_CHANNELS, &count);

    if (count == 0) {
        shell_print(sh, "No active PWM channels. Use 'pwm set <pin> <freq_hz> <duty_percent>'.");
        return 0;
    }

    shell_print(sh, "=== LEDC Hardware PWM Channels (%zu Active) ===", count);
    shell_print(sh, "Channel  GPIO Pin  Frequency (Hz)  Duty Cycle (%%)  Active");
    shell_print(sh, "-------  --------  --------------  --------------  ------");
    for (size_t i = 0; i < count; i++) {
        shell_print(sh, "   %2d       %2d       %8u Hz         %3u%%        YES",
                    list[i].channel, list[i].pin, list[i].freq_hz, list[i].duty_percent);
    }
    return 0;
}

static int cmd_pwm_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 4) {
        shell_error(sh, "Usage: pwm set <pin> <freq_hz> <duty_percent>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    uint32_t freq = (uint32_t)strtoul(argv[2], NULL, 0);
    uint32_t duty = (uint32_t)strtoul(argv[3], NULL, 0);

    int ret = hw_pwm_set(pin, freq, duty);
    if (ret != 0) {
        shell_error(sh, "Failed to start PWM on GPIO %d: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "PWM active on GPIO %d: %u Hz, %u%% duty cycle", pin, freq, duty);
    return 0;
}

static int cmd_pwm_stop(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: pwm stop <pin>");
        return -EINVAL;
    }
    int pin = atoi(argv[1]);
    int ret = hw_pwm_stop(pin);
    if (ret != 0) {
        shell_error(sh, "Failed to stop PWM on GPIO %d: %d", pin, ret);
        return ret;
    }
    shell_print(sh, "PWM stopped on GPIO %d (channel released, pin tristated)", pin);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pwm,
    SHELL_CMD(status, NULL, "Show active PWM channels: pwm status [pin]", cmd_pwm_status),
    SHELL_CMD(set, NULL, "Start PWM: pwm set <pin> <freq_hz> <duty_percent>", cmd_pwm_set),
    SHELL_CMD(stop, NULL, "Stop PWM: pwm stop <pin>", cmd_pwm_stop),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(pwm, &sub_pwm, "ESPirate Hardware PWM (LEDC) commands", cmd_pwm_status);

/* ========================================================================= */
/*                        'i2c' SHELL COMMANDS                               */
/* ========================================================================= */

static int cmd_i2c_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    hw_i2c_status_t st;
    hw_i2c_get_status(&st);

    shell_print(sh, "=== I2C Subsystem Status ===");
    shell_print(sh, "  Bus Active : %s", st.active ? "YES" : "NO");
    shell_print(sh, "  SCL Pin    : %d", st.scl_pin);
    shell_print(sh, "  SDA Pin    : %d", st.sda_pin);
    shell_print(sh, "  Speed      : %u kHz", st.speed_khz);
    shell_print(sh, "  Last Scan  : %u device(s) found", st.last_scanned_count);
    if (st.last_scanned_count > 0) {
        shell_fprintf(sh, SHELL_NORMAL, "  Addresses  : ");
        for (int i = 0; i < st.last_scanned_count; i++) {
            shell_fprintf(sh, SHELL_NORMAL, "0x%02X%s", st.last_scanned_addrs[i],
                          (i + 1 < st.last_scanned_count) ? ", " : "\n");
        }
    }
    return 0;
}

static int cmd_i2c_scan(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3) {
        shell_error(sh, "Usage: i2c scan <scl_pin> <sda_pin>");
        return -EINVAL;
    }
    int scl = atoi(argv[1]);
    int sda = atoi(argv[2]);

    shell_print(sh, "Scanning 7-bit I2C bus (SCL=%d, SDA=%d)...", scl, sda);

    uint8_t addrs[128];
    size_t count = 0;
    int ret = hw_i2c_scan(scl, sda, addrs, sizeof(addrs), &count);
    if (ret != 0) {
        shell_error(sh, "I2C scan failed: %d", ret);
        return ret;
    }

    shell_print(sh, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
    for (int row = 0; row < 128; row += 16) {
        shell_fprintf(sh, SHELL_NORMAL, "%02x: ", row);
        for (int col = 0; col < 16; col++) {
            int addr = row + col;
            if (addr < 0x08 || addr > 0x77) {
                shell_fprintf(sh, SHELL_NORMAL, "   ");
                continue;
            }
            bool present = false;
            for (size_t i = 0; i < count; i++) {
                if (addrs[i] == addr) {
                    present = true;
                    break;
                }
            }
            if (present) {
                shell_fprintf(sh, SHELL_NORMAL, "%02x ", addr);
            } else {
                shell_fprintf(sh, SHELL_NORMAL, "-- ");
            }
        }
        shell_fprintf(sh, SHELL_NORMAL, "\n");
    }

    shell_print(sh, "Scan complete: %zu device(s) found.", count);
    return 0;
}

static int cmd_i2c_read(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 5) {
        shell_error(sh, "Usage: i2c read <scl> <sda> <addr> <len>");
        return -EINVAL;
    }
    int scl = atoi(argv[1]);
    int sda = atoi(argv[2]);
    uint8_t addr = (uint8_t)strtoul(argv[3], NULL, 0);
    size_t len = (size_t)strtoul(argv[4], NULL, 0);
    if (len > 256) len = 256;

    uint8_t buf[256];
    int ret = hw_i2c_read(scl, sda, addr, buf, len);
    if (ret != 0) {
        shell_error(sh, "I2C read from 0x%02X failed: %d", addr, ret);
        return ret;
    }

    shell_print(sh, "Read %zu bytes from 0x%02X:", len, addr);
    for (size_t i = 0; i < len; i++) {
        shell_fprintf(sh, SHELL_NORMAL, "%02X ", buf[i]);
        if ((i + 1) % 16 == 0 || i + 1 == len) {
            shell_fprintf(sh, SHELL_NORMAL, "\n");
        }
    }
    return 0;
}

static int cmd_i2c_write(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 5) {
        shell_error(sh, "Usage: i2c write <scl> <sda> <addr> <b0> [b1 ...]");
        return -EINVAL;
    }
    int scl = atoi(argv[1]);
    int sda = atoi(argv[2]);
    uint8_t addr = (uint8_t)strtoul(argv[3], NULL, 0);

    uint8_t buf[64];
    size_t len = 0;
    for (size_t i = 4; i < argc && len < sizeof(buf); i++) {
        buf[len++] = (uint8_t)strtoul(argv[i], NULL, 0);
    }

    int ret = hw_i2c_write(scl, sda, addr, buf, len);
    if (ret != 0) {
        shell_error(sh, "I2C write to 0x%02X failed: %d", addr, ret);
        return ret;
    }
    shell_print(sh, "Wrote %zu bytes to I2C device 0x%02X", len, addr);
    return 0;
}

static int cmd_i2c_write_read(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 6) {
        shell_error(sh, "Usage: i2c write_read <scl> <sda> <addr> <tx_byte> <rx_len>");
        return -EINVAL;
    }
    int scl = atoi(argv[1]);
    int sda = atoi(argv[2]);
    uint8_t addr = (uint8_t)strtoul(argv[3], NULL, 0);
    uint8_t tx = (uint8_t)strtoul(argv[4], NULL, 0);
    size_t rx_len = (size_t)strtoul(argv[5], NULL, 0);
    if (rx_len > 256) rx_len = 256;

    uint8_t rx[256];
    int ret = hw_i2c_write_read(scl, sda, addr, &tx, 1, rx, rx_len);
    if (ret != 0) {
        shell_error(sh, "I2C write_read (reg 0x%02X) on 0x%02X failed: %d", tx, addr, ret);
        return ret;
    }

    shell_print(sh, "Received %zu bytes from 0x%02X:", rx_len, addr);
    for (size_t i = 0; i < rx_len; i++) {
        shell_fprintf(sh, SHELL_NORMAL, "%02X ", rx[i]);
    }
    shell_fprintf(sh, SHELL_NORMAL, "\n");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_i2c,
    SHELL_CMD(status, NULL, "Show I2C bus status: i2c status", cmd_i2c_status),
    SHELL_CMD(scan, NULL, "Scan 7-bit bus: i2c scan <scl> <sda>", cmd_i2c_scan),
    SHELL_CMD(read, NULL, "Read bytes: i2c read <scl> <sda> <addr> <len>", cmd_i2c_read),
    SHELL_CMD(write, NULL, "Write bytes: i2c write <scl> <sda> <addr> <b0> [b1 ...]", cmd_i2c_write),
    SHELL_CMD(write_read, NULL, "Write register then read: i2c write_read <scl> <sda> <addr> <tx> <rx_len>", cmd_i2c_write_read),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(i2c, &sub_i2c, "ESPirate I2C commands", cmd_i2c_status);

/* ========================================================================= */
/*                        'spi' SHELL COMMANDS                               */
/* ========================================================================= */

static int cmd_spi_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    hw_spi_status_t st;
    hw_spi_get_status(&st);

    shell_print(sh, "=== SPI Subsystem Status ===");
    shell_print(sh, "  Bus Active : %s", st.active ? "YES" : "NO");
    shell_print(sh, "  SCK Pin    : %d", st.sck_pin);
    shell_print(sh, "  MOSI Pin   : %d", st.mosi_pin);
    shell_print(sh, "  MISO Pin   : %d", st.miso_pin);
    shell_print(sh, "  CS Pin     : %d", st.cs_pin);
    shell_print(sh, "  Frequency  : %u kHz", st.freq_khz);
    shell_print(sh, "  Mode       : %u (CPOL=%d, CPHA=%d)",
                st.mode, (st.mode == 2 || st.mode == 3) ? 1 : 0, (st.mode == 1 || st.mode == 3) ? 1 : 0);
    return 0;
}

static int cmd_spi_transfer(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 7) {
        shell_error(sh, "Usage: spi transfer <sck> <mosi> <miso> <cs> <mode> <b0> [b1 ...]");
        return -EINVAL;
    }
    int sck  = atoi(argv[1]);
    int mosi = (strcmp(argv[2], "-") == 0 || strcmp(argv[2], "-1") == 0) ? -1 : atoi(argv[2]);
    int miso = (strcmp(argv[3], "-") == 0 || strcmp(argv[3], "-1") == 0) ? -1 : atoi(argv[3]);
    int cs   = (strcmp(argv[4], "-") == 0 || strcmp(argv[4], "-1") == 0) ? -1 : atoi(argv[4]);
    uint8_t mode = (uint8_t)atoi(argv[5]);

    uint8_t tx[128];
    uint8_t rx[128];
    size_t len = 0;
    for (size_t i = 6; i < argc && len < sizeof(tx); i++) {
        tx[len++] = (uint8_t)strtoul(argv[i], NULL, 0);
    }

    int ret = hw_spi_transfer(sck, mosi, miso, cs, mode, tx, rx, len);
    if (ret != 0) {
        shell_error(sh, "SPI transfer failed: %d", ret);
        return ret;
    }

    shell_print(sh, "SPI Transfer Result (%zu bytes):", len);
    shell_fprintf(sh, SHELL_NORMAL, "  TX: ");
    for (size_t i = 0; i < len; i++) shell_fprintf(sh, SHELL_NORMAL, "%02X ", tx[i]);
    shell_fprintf(sh, SHELL_NORMAL, "\n  RX: ");
    for (size_t i = 0; i < len; i++) shell_fprintf(sh, SHELL_NORMAL, "%02X ", rx[i]);
    shell_fprintf(sh, SHELL_NORMAL, "\n");
    return 0;
}

static int cmd_spi_write(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 6) {
        shell_error(sh, "Usage: spi write <sck> <mosi> <cs> <mode> <b0> [b1 ...]");
        return -EINVAL;
    }
    int sck  = atoi(argv[1]);
    int mosi = atoi(argv[2]);
    int cs   = (strcmp(argv[3], "-") == 0 || strcmp(argv[3], "-1") == 0) ? -1 : atoi(argv[3]);
    uint8_t mode = (uint8_t)atoi(argv[4]);

    uint8_t tx[128];
    size_t len = 0;
    for (size_t i = 5; i < argc && len < sizeof(tx); i++) {
        tx[len++] = (uint8_t)strtoul(argv[i], NULL, 0);
    }

    int ret = hw_spi_write(sck, mosi, cs, mode, tx, len);
    if (ret != 0) {
        shell_error(sh, "SPI write failed: %d", ret);
        return ret;
    }
    shell_print(sh, "Transmitted %zu bytes over SPI", len);
    return 0;
}

static int cmd_spi_read(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 6) {
        shell_error(sh, "Usage: spi read <sck> <miso> <cs> <mode> <len>");
        return -EINVAL;
    }
    int sck  = atoi(argv[1]);
    int miso = atoi(argv[2]);
    int cs   = (strcmp(argv[3], "-") == 0 || strcmp(argv[3], "-1") == 0) ? -1 : atoi(argv[3]);
    uint8_t mode = (uint8_t)atoi(argv[4]);
    size_t len = (size_t)strtoul(argv[5], NULL, 0);
    if (len > 256) len = 256;

    uint8_t rx[256];
    int ret = hw_spi_read(sck, miso, cs, mode, rx, len);
    if (ret != 0) {
        shell_error(sh, "SPI read failed: %d", ret);
        return ret;
    }
    shell_print(sh, "Read %zu bytes from SPI:", len);
    for (size_t i = 0; i < len; i++) {
        shell_fprintf(sh, SHELL_NORMAL, "%02X ", rx[i]);
    }
    shell_fprintf(sh, SHELL_NORMAL, "\n");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_spi,
    SHELL_CMD(status, NULL, "Show SPI bus status: spi status", cmd_spi_status),
    SHELL_CMD(transfer, NULL, "Full duplex transfer: spi transfer <sck> <mosi> <miso> <cs> <mode> <b0> [b1 ...]", cmd_spi_transfer),
    SHELL_CMD(write, NULL, "Write bytes: spi write <sck> <mosi> <cs> <mode> <b0> [b1 ...]", cmd_spi_write),
    SHELL_CMD(read, NULL, "Read bytes: spi read <sck> <miso> <cs> <mode> <len>", cmd_spi_read),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(spi, &sub_spi, "ESPirate SPI commands", cmd_spi_status);

/* ========================================================================= */
/*                          'lua' SHELL COMMAND                              */
/* ========================================================================= */

static int cmd_lua(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: lua \"<code>\" | lua run <file> | lua ls | lua cat <file> | lua rm <file> | lua status | lua reset");
        return -EINVAL;
    }

    if (argc >= 2 && strcmp(argv[1], "ls") == 0) {
        return cmd_storage_ls(sh, argc - 1, argv + 1);
    }

    if (argc >= 3 && strcmp(argv[1], "cat") == 0) {
        return cmd_storage_cat(sh, argc - 1, argv + 1);
    }

    if (argc >= 3 && strcmp(argv[1], "rm") == 0) {
        return cmd_storage_rm(sh, argc - 1, argv + 1);
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

    if (argc >= 2 && (strcmp(argv[1], "stop") == 0 || strcmp(argv[1], "abort") == 0)) {
        if (!lua_worker_is_busy()) {
            shell_print(sh, "No Lua script is currently running.");
            return 0;
        }
        shell_print(sh, "Stopping running Lua script...");
        lua_manager_interrupt();
        return 0;
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

static int cmd_lua_stop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!lua_worker_is_busy()) {
        shell_print(sh, "No Lua script is currently running.");
        return 0;
    }
    shell_print(sh, "Stopping running Lua script...");
    lua_manager_interrupt();
    return 0;
}

SHELL_CMD_REGISTER(lua, NULL, "Execute Lua code: lua \"<code>\" | lua run <file> | lua bg <file/code> | lua stop | lua status | lua reset", cmd_lua);
SHELL_CMD_REGISTER(abort, NULL, "Interrupt and stop running Lua script", cmd_lua_stop);
SHELL_CMD_REGISTER(stop, NULL, "Interrupt and stop running Lua script", cmd_lua_stop);

void espirate_shell_init(void)
{
}
