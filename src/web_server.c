/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "web_server.h"
#include "web_dashboard.h"
#include "fs_manager.h"
#include "lua_manager.h"
#include "lua_worker.h"
#include "wifi_manager.h"
#include "hw_gpio.h"
#include "hw_pwm.h"
#include "hw_i2c.h"
#include "hw_spi.h"

LOG_MODULE_REGISTER(web_server, LOG_LEVEL_INF);

#define WEB_SERVER_PORT 80
#define REQ_BUF_SIZE    8192

K_THREAD_STACK_DEFINE(web_server_stack, 8192);
static struct k_thread web_server_thread_data;
static bool s_running = false;

static void send_http_response(int sock, int status_code, const char *content_type,
                               const char *body, size_t body_len)
{
    char hdr[256];
    const char *status_str = "OK";
    if (status_code == 200) status_str = "OK";
    else if (status_code == 204) status_str = "No Content";
    else if (status_code == 400) status_str = "Bad Request";
    else if (status_code == 404) status_str = "Not Found";
    else if (status_code == 405) status_str = "Method Not Allowed";
    else if (status_code == 500) status_str = "Internal Server Error";

    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        status_code, status_str, content_type, body_len);

    ssize_t ret = zsock_send(sock, hdr, hdr_len, 0);
    if (ret <= 0) {
        return;
    }

    if (body && body_len > 0) {
        size_t sent_total = 0;
        while (sent_total < body_len) {
            size_t chunk = body_len - sent_total;
            if (chunk > 1024) {
                chunk = 1024;
            }
            ret = zsock_send(sock, body + sent_total, chunk, 0);
            if (ret <= 0) {
                break;
            }
            sent_total += ret;
        }
    }
}

static void handle_api_status(int sock)
{
    char json[512];
    espirate_telemetry_t t;
    espirate_telemetry_get(&t);

    int len = snprintf(json, sizeof(json),
        "{"
        "\"uptime_ms\":%lld,"
        "\"lua_mem_kb\":%zu,"
        "\"worker_busy\":%s,"
        "\"fs_mounted\":%s,"
        "\"wifi_mode\":\"%s\","
        "\"wifi_clients\":%u,"
        "\"ssid\":\"%s\","
        "\"sta_connected\":%s,"
        "\"sta_ip\":\"%s\","
        "\"hostname\":\"%s\","
        "\"fake_internet\":%s"
        "}",
        (long long)k_uptime_get(),
        lua_manager_get_memory_kb(),
        lua_worker_is_busy() ? "true" : "false",
        fs_manager_is_mounted() ? "true" : "false",
        wifi_manager_get_mode_str(),
        wifi_manager_get_station_count(),
        wifi_manager_get_ssid(),
        wifi_manager_sta_is_connected() ? "true" : "false",
        wifi_manager_get_sta_ip(),
        wifi_manager_get_hostname(),
        wifi_manager_get_fake_internet() ? "true" : "false");

    send_http_response(sock, 200, "application/json", json, len);
}

static void handle_api_wifi_get(int sock)
{
    char json[512];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"mode\":\"%s\","
        "\"configured_mode\":\"%s\","
        "\"ap\":{\"ssid\":\"%s\",\"ip\":\"%s\",\"clients\":%u},"
        "\"sta\":{\"configured\":%s,\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\"},"
        "\"hostname\":\"%s\","
        "\"mdns\":\"%s\","
        "\"fake_internet\":%s"
        "}",
        wifi_manager_get_mode_str(),
        (wifi_manager_get_configured_mode() == ESPIRATE_WIFI_MODE_STA) ? "STA" : "AP",
        wifi_manager_get_ap_ssid(),
        wifi_manager_get_ap_ip(),
        wifi_manager_get_station_count(),
        wifi_manager_has_saved_sta() ? "true" : "false",
        wifi_manager_sta_is_connected() ? "true" : "false",
        wifi_manager_get_sta_ssid(),
        wifi_manager_get_sta_ip(),
        wifi_manager_get_hostname(),
        wifi_manager_get_mdns_domain(),
        wifi_manager_get_fake_internet() ? "true" : "false");

    send_http_response(sock, 200, "application/json", json, len);
}

static void extract_json_str(const char *json, const char *key, char *out, size_t max_out)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    char *p = strstr(json, needle);
    if (!p) {
        out[0] = '\0';
        return;
    }
    p += strlen(needle);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (*p != '"') {
        out[0] = '\0';
        return;
    }
    p++; /* skip opening quote */

    size_t i = 0;
    while (*p && i < max_out - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            if (*p == 'n') out[i++] = '\n';
            else if (*p == 'r') out[i++] = '\r';
            else if (*p == 't') out[i++] = '\t';
            else out[i++] = *p;
            p++;
            continue;
        }
        if (*p == '"') break;
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static int extract_json_int(const char *json, const char *key, int default_val)
{
    if (!json || !key) return default_val;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    char *p = strstr(json, needle);
    if (!p) return default_val;
    p += strlen(needle);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    if (*p == '"') p++;
    return (int)strtol(p, NULL, 0);
}

static int extract_json_int_array(const char *json, const char *key, uint8_t *out, size_t max_out, size_t *count)
{
    if (!json || !key || !out) {
        if (count) *count = 0;
        return -EINVAL;
    }
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    char *p = strstr(json, needle);
    if (!p) {
        if (count) *count = 0;
        return -ENOENT;
    }
    p = strchr(p, '[');
    if (!p) {
        if (count) *count = 0;
        return -EINVAL;
    }
    p++;
    size_t n = 0;
    while (*p && *p != ']' && n < max_out) {
        while (*p && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (*p == ']' || !*p) break;
        out[n++] = (uint8_t)strtoul(p, &p, 0);
    }
    if (count) *count = n;
    return 0;
}

static void handle_api_wifi_post(int sock, const char *body)
{
    char ap_ssid[64] = {0};
    char ssid[64] = {0};
    char pass[64] = {0};
    char sec[16] = {0};
    extract_json_str(body, "ap_ssid", ap_ssid, sizeof(ap_ssid));
    extract_json_str(body, "ssid", ssid, sizeof(ssid));
    extract_json_str(body, "password", pass, sizeof(pass));
    extract_json_str(body, "security", sec, sizeof(sec));

    bool updated = false;

    if (strlen(ap_ssid) > 0) {
        wifi_manager_set_ap_ssid(ap_ssid, true);
        updated = true;
    }

    if (strlen(ssid) > 0) {
        wifi_manager_set_sta_credentials(ssid, pass, (strlen(sec) > 0) ? sec : NULL, true);
        send_http_response(sock, 200, "application/json", "{\"status\":\"connecting\"}", 23);
        k_msleep(50);
        wifi_manager_set_mode(ESPIRATE_WIFI_MODE_STA, true);
        return;
    }

    if (updated) {
        send_http_response(sock, 200, "application/json", "{\"status\":\"ok\"}", 15);
        return;
    }

    send_http_response(sock, 400, "text/plain", "Missing ssid or ap_ssid", 23);
}

static void handle_api_wifi_mode(int sock, const char *body)
{
    char mode[16] = {0};
    extract_json_str(body, "mode", mode, sizeof(mode));

    if (strcasecmp(mode, "sta") == 0) {
        if (!wifi_manager_has_saved_sta()) {
            send_http_response(sock, 400, "application/json", "{\"error\":\"No saved STA credentials\"}", 36);
            return;
        }
        send_http_response(sock, 200, "application/json", "{\"status\":\"switching_to_sta\"}", 30);
        k_msleep(50);
        wifi_manager_set_mode(ESPIRATE_WIFI_MODE_STA, true);
    } else if (strcasecmp(mode, "ap") == 0) {
        send_http_response(sock, 200, "application/json", "{\"status\":\"switching_to_ap\"}", 29);
        k_msleep(50);
        wifi_manager_set_mode(ESPIRATE_WIFI_MODE_AP, true);
    } else {
        send_http_response(sock, 400, "application/json", "{\"error\":\"Invalid mode\"}", 24);
    }
}

static void handle_api_wifi_config(int sock, const char *body)
{
    if (strstr(body, "\"fake_internet\":true") != NULL ||
        strstr(body, "\"fake_internet\": true") != NULL) {
        wifi_manager_set_fake_internet(true);
    } else if (strstr(body, "\"fake_internet\":false") != NULL ||
               strstr(body, "\"fake_internet\": false") != NULL) {
        wifi_manager_set_fake_internet(false);
    }
    send_http_response(sock, 200, "application/json", "{\"status\":\"ok\"}", 15);
}

static void handle_api_wifi_forget(int sock)
{
    wifi_manager_forget_sta();
    send_http_response(sock, 200, "application/json", "{\"status\":\"forgotten\"}", 22);
}

static void handle_api_telemetry(int sock)
{
    char json[256];
    espirate_telemetry_t t;
    espirate_telemetry_get(&t);

    int len = snprintf(json, sizeof(json),
        "{\"total\":%u,\"pass\":%u,\"fail\":%u,\"status\":\"%s\"}",
        t.total_cycles, t.passed_cycles, t.failed_cycles, t.last_status);

    send_http_response(sock, 200, "application/json", json, len);
}

static void handle_api_scripts_list(int sock)
{
    static char resp[4096];
    size_t pos = 0;
    pos += snprintf(resp + pos, sizeof(resp) - pos, "[");

    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    if (fs_opendir(&dir, ESPIRATE_FS_MOUNT_POINT) == 0) {
        struct fs_dirent entry;
        bool first = true;
        while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
            if (entry.type == FS_DIR_ENTRY_FILE) {
                if (!first && pos < sizeof(resp) - 64) {
                    pos += snprintf(resp + pos, sizeof(resp) - pos, ",");
                }
                pos += snprintf(resp + pos, sizeof(resp) - pos,
                                "{\"name\":\"%s\",\"size\":%zu}", entry.name, entry.size);
                first = false;
            }
        }
        fs_closedir(&dir);
    }

    if (pos < sizeof(resp) - 2) {
        pos += snprintf(resp + pos, sizeof(resp) - pos, "]");
    }

    send_http_response(sock, 200, "application/json", resp, pos);
}

static void handle_api_get_script(int sock, const char *path)
{
    const char *name_param = strstr(path, "name=");
    if (!name_param) {
        send_http_response(sock, 400, "text/plain", "Missing name parameter", 22);
        return;
    }
    name_param += 5;

    char filename[128] = {0};
    size_t i = 0;
    while (*name_param && *name_param != '&' && i < sizeof(filename) - 1) {
        filename[i++] = *name_param++;
    }

    char full_path[160];
    if (filename[0] == '/') {
        strncpy(full_path, filename, sizeof(full_path) - 1);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", ESPIRATE_FS_MOUNT_POINT, filename);
    }

    static char buf[3072];
    size_t bytes_read = 0;
    int rc = fs_manager_read_file(full_path, buf, sizeof(buf) - 1, &bytes_read);
    if (rc != 0) {
        send_http_response(sock, 404, "text/plain", "Script not found", 16);
        return;
    }
    buf[bytes_read] = '\0';

    send_http_response(sock, 200, "text/plain", buf, bytes_read);
}

static void handle_api_save_script(int sock, const char *body)
{
    char name[128] = {0};
    static char content[8192];
    memset(content, 0, sizeof(content));

    extract_json_str(body, "name", name, sizeof(name));
    extract_json_str(body, "content", content, sizeof(content));

    if (strlen(name) == 0) {
        send_http_response(sock, 400, "text/plain", "Missing script name", 19);
        return;
    }

    char full_path[160];
    if (name[0] == '/') {
        strncpy(full_path, name, sizeof(full_path) - 1);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", ESPIRATE_FS_MOUNT_POINT, name);
    }

    int rc = fs_manager_write_file(full_path, content, strlen(content));
    if (rc != 0) {
        send_http_response(sock, 500, "text/plain", "Failed to write script to flash", 31);
        return;
    }

    send_http_response(sock, 200, "application/json", "{\"status\":\"saved\"}", 18);
}

static void handle_api_delete_script(int sock, const char *body, const char *path)
{
    char name[128] = {0};
    if (body && strlen(body) > 0) {
        extract_json_str(body, "name", name, sizeof(name));
    }
    if (strlen(name) == 0 && path) {
        const char *name_param = strstr(path, "name=");
        if (name_param) {
            name_param += 5;
            size_t i = 0;
            while (*name_param && *name_param != '&' && i < sizeof(name) - 1) {
                name[i++] = *name_param++;
            }
        }
    }

    if (strlen(name) == 0) {
        send_http_response(sock, 400, "text/plain", "Missing file name", 17);
        return;
    }

    int rc = fs_manager_delete_file(name);
    if (rc != 0) {
        send_http_response(sock, 500, "text/plain", "Failed to delete file", 21);
        return;
    }

    send_http_response(sock, 200, "application/json", "{\"status\":\"deleted\"}", 20);
}

static void handle_api_rename_script(int sock, const char *body)
{
    char old_name[128] = {0};
    char new_name[128] = {0};

    extract_json_str(body, "old_name", old_name, sizeof(old_name));
    if (strlen(old_name) == 0) {
        extract_json_str(body, "src", old_name, sizeof(old_name));
    }
    extract_json_str(body, "new_name", new_name, sizeof(new_name));
    if (strlen(new_name) == 0) {
        extract_json_str(body, "dest", new_name, sizeof(new_name));
    }

    if (strlen(old_name) == 0 || strlen(new_name) == 0) {
        send_http_response(sock, 400, "text/plain", "Missing old_name or new_name", 28);
        return;
    }

    int rc = fs_manager_rename_file(old_name, new_name);
    if (rc != 0) {
        send_http_response(sock, 500, "text/plain", "Failed to rename file", 21);
        return;
    }

    send_http_response(sock, 200, "application/json", "{\"status\":\"renamed\"}", 20);
}

static void handle_api_storage(int sock)
{
    size_t total = 0, free_b = 0;
    int rc = fs_manager_statvfs(&total, &free_b);
    char json[256];
    if (rc == 0) {
        size_t used = (total >= free_b) ? (total - free_b) : 0;
        uint32_t chip_size = fs_manager_get_chip_size();
        int len = snprintf(json, sizeof(json),
            "{\"mounted\":true,\"mount_point\":\"%s\",\"total_bytes\":%zu,\"free_bytes\":%zu,\"used_bytes\":%zu,\"chip_size_mb\":%u}",
            ESPIRATE_FS_MOUNT_POINT, total, free_b, used, chip_size / (1024 * 1024));
        send_http_response(sock, 200, "application/json", json, len);
    } else {
        int len = snprintf(json, sizeof(json), "{\"mounted\":false,\"error\":%d}", rc);
        send_http_response(sock, 500, "application/json", json, len);
    }
}

static void handle_api_storage_format(int sock)
{
    int rc = fs_manager_format();
    if (rc == 0) {
        send_http_response(sock, 200, "application/json", "{\"status\":\"formatted\"}", 22);
    } else {
        send_http_response(sock, 500, "text/plain", "Format failed", 13);
    }
}

static void handle_api_run(int sock, const char *body)
{
    char file[160] = {0};
    char code[512] = {0};
    bool bg = (strstr(body, "\"bg\":true") != NULL || strstr(body, "\"bg\": true") != NULL);

    extract_json_str(body, "file", file, sizeof(file));
    extract_json_str(body, "code", code, sizeof(code));

    lua_job_t job = {
        .sh = NULL,
        .done_sem = NULL,
        .result = 0,
    };

    if (strlen(file) > 0) {
        job.type = LUA_JOB_EVAL_FILE;
        strncpy(job.payload, file, sizeof(job.payload) - 1);
    } else if (strlen(code) > 0) {
        job.type = LUA_JOB_EVAL_STRING;
        strncpy(job.payload, code, sizeof(job.payload) - 1);
    } else {
        send_http_response(sock, 400, "text/plain", "Specify file or code", 20);
        return;
    }

    if (bg) {
        int ret = lua_worker_submit_async(&job);
        if (ret == 0) {
            send_http_response(sock, 200, "text/plain", "[Worker] Job submitted to background thread.", 44);
        } else {
            send_http_response(sock, 500, "text/plain", "[Worker] Queue full or worker error.", 36);
        }
    } else {
        int ret;
        if (job.type == LUA_JOB_EVAL_FILE) {
            ret = lua_worker_eval_file(job.payload, NULL);
        } else {
            ret = lua_worker_eval(job.payload, NULL);
        }
        if (ret == 0) {
            send_http_response(sock, 200, "text/plain", "Execution completed successfully.", 33);
        } else {
            send_http_response(sock, 500, "text/plain", "Execution error.", 16);
        }
    }
}

/* ========================================================================= */
/*                   HARDWARE REST API HANDLERS                              */
/* ========================================================================= */

static void handle_api_gpio_get(int sock, const char *path)
{
    const char *p = strstr(path, "pin=");
    if (p) {
        int pin = atoi(p + 4);
        char mode_buf[64] = {0};
        int level = -1;
        int ret = hw_gpio_get_state(pin, mode_buf, sizeof(mode_buf), &level);
        if (ret != 0) {
            send_http_response(sock, 400, "application/json", "{\"error\":\"Invalid or reserved pin\"}", 34);
            return;
        }
        char json[256];
        int len = snprintf(json, sizeof(json),
            "{\"pin\":%d,\"mode\":\"%s\",\"level\":%d}",
            pin, mode_buf, level);
        send_http_response(sock, 200, "application/json", json, len);
        return;
    }

    static char buf[3072];
    size_t pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[");
    bool first = true;
    for (int pin = 0; pin <= 48; pin++) {
        if (hw_gpio_check_safety(pin, NULL) != 0) continue;
        char mode_buf[64] = {0};
        int level = -1;
        hw_gpio_get_state(pin, mode_buf, sizeof(mode_buf), &level);
        if (!first && pos < sizeof(buf) - 64) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "{\"pin\":%d,\"mode\":\"%s\",\"level\":%d}",
                        pin, mode_buf, level);
        first = false;
    }
    if (pos < sizeof(buf) - 2) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "]");
    }
    send_http_response(sock, 200, "application/json", buf, pos);
}

static void handle_api_gpio_post(int sock, const char *body)
{
    int pin = extract_json_int(body, "pin", -1);
    if (pin < 0) {
        send_http_response(sock, 400, "application/json", "{\"error\":\"Missing pin parameter\"}", 32);
        return;
    }

    char action[32] = {0};
    char mode[32] = {0};
    char pull[32] = {0};
    extract_json_str(body, "action", action, sizeof(action));
    extract_json_str(body, "mode", mode, sizeof(mode));
    extract_json_str(body, "pull", pull, sizeof(pull));

    int val = extract_json_int(body, "value", -1);
    if (val < 0) val = extract_json_int(body, "level", -1);

    int ret = 0;
    if (strcmp(action, "mode") == 0 || strlen(mode) > 0) {
        ret = hw_gpio_mode(pin, mode[0] ? mode : "out", pull[0] ? pull : NULL);
    } else if (strcmp(action, "write") == 0 || val >= 0) {
        ret = hw_gpio_write(pin, val > 0 ? 1 : 0);
    } else if (strcmp(action, "high") == 0) {
        ret = hw_gpio_high(pin);
    } else if (strcmp(action, "low") == 0) {
        ret = hw_gpio_low(pin);
    } else if (strcmp(action, "toggle") == 0) {
        ret = hw_gpio_toggle(pin);
    } else if (strcmp(action, "tristate") == 0 || strcmp(action, "hiz") == 0) {
        ret = hw_gpio_tristate(pin);
    } else if (strcmp(action, "pull") == 0) {
        ret = hw_gpio_pull(pin, pull[0] ? pull : "none");
    } else if (strcmp(action, "drive") == 0) {
        int ma = extract_json_int(body, "ma", 20);
        ret = hw_gpio_drive(pin, ma);
    } else {
        ret = -EINVAL;
    }

    if (ret != 0) {
        char err[128];
        int len = snprintf(err, sizeof(err), "{\"error\":\"Operation failed\",\"code\":%d}", ret);
        send_http_response(sock, 400, "application/json", err, len);
        return;
    }

    char mode_buf[64] = {0};
    int level = -1;
    hw_gpio_get_state(pin, mode_buf, sizeof(mode_buf), &level);

    char resp[256];
    int len = snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"pin\":%d,\"mode\":\"%s\",\"level\":%d}",
        pin, mode_buf, level);
    send_http_response(sock, 200, "application/json", resp, len);
}

static void handle_api_pwm_get(int sock)
{
    hw_pwm_status_t list[HW_PWM_MAX_CHANNELS];
    size_t count = 0;
    hw_pwm_get_all_status(list, HW_PWM_MAX_CHANNELS, &count);

    char buf[512];
    size_t pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && pos < sizeof(buf) - 64) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "{\"channel\":%d,\"pin\":%d,\"freq_hz\":%u,\"duty_percent\":%u}",
            list[i].channel, list[i].pin, list[i].freq_hz, list[i].duty_percent);
    }
    if (pos < sizeof(buf) - 2) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "]");
    }
    send_http_response(sock, 200, "application/json", buf, pos);
}

static void handle_api_pwm_post(int sock, const char *body)
{
    char action[32] = {0};
    extract_json_str(body, "action", action, sizeof(action));
    int pin = extract_json_int(body, "pin", -1);
    if (pin < 0) {
        send_http_response(sock, 400, "application/json", "{\"error\":\"Missing pin\"}", 22);
        return;
    }

    if (strcmp(action, "stop") == 0) {
        int ret = hw_pwm_stop(pin);
        if (ret != 0) {
            send_http_response(sock, 400, "application/json", "{\"error\":\"PWM not active on pin\"}", 32);
            return;
        }
        send_http_response(sock, 200, "application/json", "{\"status\":\"stopped\"}", 20);
        return;
    }

    int freq = extract_json_int(body, "freq", 1000);
    if (freq <= 0) freq = extract_json_int(body, "freq_hz", 1000);
    int duty = extract_json_int(body, "duty", 50);
    if (duty < 0) duty = extract_json_int(body, "duty_percent", 50);

    int ret = hw_pwm_set(pin, (uint32_t)freq, (uint32_t)duty);
    if (ret != 0) {
        char err[128];
        int len = snprintf(err, sizeof(err), "{\"error\":\"Failed to set PWM\",\"code\":%d}", ret);
        send_http_response(sock, 400, "application/json", err, len);
        return;
    }

    char resp[256];
    int len = snprintf(resp, sizeof(resp),
        "{\"status\":\"ok\",\"pin\":%d,\"freq_hz\":%d,\"duty_percent\":%d}",
        pin, freq, duty);
    send_http_response(sock, 200, "application/json", resp, len);
}

static void handle_api_i2c_get(int sock)
{
    hw_i2c_status_t st;
    hw_i2c_get_status(&st);

    char buf[512];
    size_t pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "{\"scl\":%d,\"sda\":%d,\"speed_khz\":%u,\"active\":%s,\"devices\":[",
        st.scl_pin, st.sda_pin, st.speed_khz, st.active ? "true" : "false");
    for (int i = 0; i < st.last_scanned_count; i++) {
        if (i > 0 && pos < sizeof(buf) - 32) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\"0x%02X\"", st.last_scanned_addrs[i]);
    }
    if (pos < sizeof(buf) - 4) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    }
    send_http_response(sock, 200, "application/json", buf, pos);
}

static void handle_api_i2c_scan(int sock, const char *body)
{
    int scl = extract_json_int(body, "scl", -1);
    int sda = extract_json_int(body, "sda", -1);
    if (scl < 0 || sda < 0) {
        send_http_response(sock, 400, "application/json", "{\"error\":\"Missing scl or sda\"}", 28);
        return;
    }

    uint8_t addrs[128];
    size_t count = 0;
    int ret = hw_i2c_scan(scl, sda, addrs, sizeof(addrs), &count);
    if (ret != 0) {
        char err[128];
        int len = snprintf(err, sizeof(err), "{\"error\":\"Scan failed\",\"code\":%d}", ret);
        send_http_response(sock, 400, "application/json", err, len);
        return;
    }

    char buf[512];
    size_t pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"status\":\"ok\",\"found\":[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && pos < sizeof(buf) - 32) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos, "\"0x%02X\"", addrs[i]);
    }
    if (pos < sizeof(buf) - 4) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    }
    send_http_response(sock, 200, "application/json", buf, pos);
}

static void handle_api_i2c_write(int sock, const char *body)
{
    int scl = extract_json_int(body, "scl", -1);
    int sda = extract_json_int(body, "sda", -1);
    int addr = extract_json_int(body, "addr", -1);
    uint8_t data[128];
    size_t len = 0;
    extract_json_int_array(body, "data", data, sizeof(data), &len);

    if (scl < 0 || sda < 0 || addr < 0) {
        send_http_response(sock, 400, "application/json", "{\"error\":\"Missing parameters\"}", 28);
        return;
    }

    int ret = hw_i2c_write(scl, sda, (uint8_t)addr, data, len);
    if (ret != 0) {
        char err[128];
        int l = snprintf(err, sizeof(err), "{\"error\":\"I2C write failed\",\"code\":%d}", ret);
        send_http_response(sock, 400, "application/json", err, l);
        return;
    }
    send_http_response(sock, 200, "application/json", "{\"status\":\"ok\"}", 15);
}

static void handle_api_i2c_read(int sock, const char *body)
{
    int scl = extract_json_int(body, "scl", -1);
    int sda = extract_json_int(body, "sda", -1);
    int addr = extract_json_int(body, "addr", -1);
    int len = extract_json_int(body, "len", 1);
    if (scl < 0 || sda < 0 || addr < 0 || len <= 0 || len > 256) {
        send_http_response(sock, 400, "application/json", "{\"error\":\"Invalid parameters\"}", 28);
        return;
    }

    uint8_t buf[256];
    int ret = hw_i2c_read(scl, sda, (uint8_t)addr, buf, (size_t)len);
    if (ret != 0) {
        char err[128];
        int l = snprintf(err, sizeof(err), "{\"error\":\"I2C read failed\",\"code\":%d}", ret);
        send_http_response(sock, 400, "application/json", err, l);
        return;
    }

    char resp[1024];
    size_t pos = 0;
    pos += snprintf(resp + pos, sizeof(resp) - pos, "{\"status\":\"ok\",\"data\":[");
    for (int i = 0; i < len; i++) {
        if (i > 0 && pos < sizeof(resp) - 16) pos += snprintf(resp + pos, sizeof(resp) - pos, ",");
        pos += snprintf(resp + pos, sizeof(resp) - pos, "%u", buf[i]);
    }
    if (pos < sizeof(resp) - 4) pos += snprintf(resp + pos, sizeof(resp) - pos, "]}");
    send_http_response(sock, 200, "application/json", resp, pos);
}

static void handle_api_spi_get(int sock)
{
    hw_spi_status_t st;
    hw_spi_get_status(&st);

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"sck\":%d,\"mosi\":%d,\"miso\":%d,\"cs\":%d,\"freq_khz\":%u,\"mode\":%u,\"active\":%s}",
        st.sck_pin, st.mosi_pin, st.miso_pin, st.cs_pin, st.freq_khz, st.mode, st.active ? "true" : "false");
    send_http_response(sock, 200, "application/json", buf, len);
}

static void handle_api_spi_transfer(int sock, const char *body)
{
    int sck = extract_json_int(body, "sck", -1);
    int mosi = extract_json_int(body, "mosi", -1);
    int miso = extract_json_int(body, "miso", -1);
    int cs = extract_json_int(body, "cs", -1);
    int mode = extract_json_int(body, "mode", 0);

    uint8_t tx[128];
    uint8_t rx[128];
    size_t len = 0;
    extract_json_int_array(body, "data", tx, sizeof(tx), &len);
    if (len == 0) len = 1;

    int ret = hw_spi_transfer(sck, mosi, miso, cs, (uint8_t)mode, tx, rx, len);
    if (ret != 0) {
        char err[128];
        int l = snprintf(err, sizeof(err), "{\"error\":\"SPI transfer failed\",\"code\":%d}", ret);
        send_http_response(sock, 400, "application/json", err, l);
        return;
    }

    char resp[1024];
    size_t pos = 0;
    pos += snprintf(resp + pos, sizeof(resp) - pos, "{\"status\":\"ok\",\"rx\":[");
    for (size_t i = 0; i < len; i++) {
        if (i > 0 && pos < sizeof(resp) - 16) pos += snprintf(resp + pos, sizeof(resp) - pos, ",");
        pos += snprintf(resp + pos, sizeof(resp) - pos, "%u", rx[i]);
    }
    if (pos < sizeof(resp) - 4) pos += snprintf(resp + pos, sizeof(resp) - pos, "]}");
    send_http_response(sock, 200, "application/json", resp, pos);
}

static void web_server_thread_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    /* Delay to allow Wi-Fi interface initialization */
    k_sleep(K_MSEC(1000));

    int server_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        LOG_ERR("Failed to create TCP socket: %d", errno);
        return;
    }

    int opt = 1;
    zsock_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(WEB_SERVER_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (zsock_bind(server_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Failed to bind socket to port %d: %d", WEB_SERVER_PORT, errno);
        zsock_close(server_fd);
        return;
    }

    if (zsock_listen(server_fd, 5) < 0) {
        LOG_ERR("Failed to listen on socket: %d", errno);
        zsock_close(server_fd);
        return;
    }

    s_running = true;
    LOG_INF("ESPirate Web Server active on port %d (http://%s)", WEB_SERVER_PORT, wifi_manager_get_ip());

    static char req_buf[REQ_BUF_SIZE];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = zsock_accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            k_msleep(50);
            continue;
        }

        /* Wait up to 300ms for incoming data. If client socket is an idle browser pre-connect,
         * close it immediately so we do not stall the single-threaded server. */
        struct zsock_pollfd pfd = {
            .fd = client_fd,
            .events = ZSOCK_POLLIN,
        };
        int poll_ret = zsock_poll(&pfd, 1, 300);
        if (poll_ret <= 0) {
            zsock_close(client_fd);
            continue;
        }

        struct timeval tv = {
            .tv_sec = 2,
            .tv_usec = 0,
        };
        zsock_setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        zsock_setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        ssize_t received = zsock_recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
        if (received > 0) {
            req_buf[received] = '\0';

            /* If Content-Length is present, receive full body across TCP segments */
            char *cl = strstr(req_buf, "Content-Length:");
            if (!cl) cl = strstr(req_buf, "content-length:");
            if (cl) {
                int content_len = atoi(cl + 15);
                char *body_start = strstr(req_buf, "\r\n\r\n");
                if (body_start && content_len > 0) {
                    size_t header_len = (body_start + 4) - req_buf;
                    size_t target_len = header_len + content_len;
                    if (target_len > sizeof(req_buf) - 1) {
                        target_len = sizeof(req_buf) - 1;
                    }
                    while (received < target_len) {
                        ssize_t r = zsock_recv(client_fd, req_buf + received, target_len - received, 0);
                        if (r <= 0) break;
                        received += r;
                    }
                    req_buf[received] = '\0';
                }
            }

            char method[8] = {0};
            char path[128] = {0};
            sscanf(req_buf, "%7s %127s", method, path);

            /* Normalize absolute URI (e.g. "http://192.168.0.56/path" -> "/path") */
            char *req_path = path;
            if (strncmp(req_path, "http://", 7) == 0) {
                req_path = strchr(req_path + 7, '/');
                if (!req_path) req_path = "/";
            } else if (strncmp(req_path, "https://", 8) == 0) {
                req_path = strchr(req_path + 8, '/');
                if (!req_path) req_path = "/";
            }

            /* Strip query string for endpoint routing */
            char clean_path[128];
            strncpy(clean_path, req_path, sizeof(clean_path) - 1);
            clean_path[sizeof(clean_path) - 1] = '\0';
            char *qmark = strchr(clean_path, '?');
            if (qmark) {
                *qmark = '\0';
            }

            LOG_INF("HTTP %s %s", method, clean_path);

            /* Locate HTTP body (after \r\n\r\n) */
            char *body = strstr(req_buf, "\r\n\r\n");
            if (body) {
                body += 4;
            } else {
                body = "";
            }

            if (strcmp(method, "GET") == 0) {
                /* Android 17 / Chrome 204 Probe */
                if (strcmp(clean_path, "/generate_204") == 0 || strcmp(clean_path, "/gen_204") == 0 ||
                    strstr(clean_path, "generate_204") != NULL || strstr(clean_path, "gen_204") != NULL) {
                    send_http_response(client_fd, 204, "text/plain", NULL, 0);
                }
                /* Favicon - 204 No Content for instant response */
                else if (strcmp(clean_path, "/favicon.ico") == 0) {
                    send_http_response(client_fd, 204, "image/x-icon", NULL, 0);
                }
                /* Microsoft NCSI Probes */
                else if (strcmp(clean_path, "/ncsi.txt") == 0) {
                    send_http_response(client_fd, 200, "text/plain", "Microsoft NCSI", 14);
                } else if (strcmp(clean_path, "/connecttest.txt") == 0) {
                    send_http_response(client_fd, 200, "text/plain", "Microsoft Connect Test", 22);
                }
                /* Apple Captive Portal Detection (only in AP mode with fake internet enabled) */
                else if ((strcmp(clean_path, "/hotspot-detect.html") == 0 ||
                          strcmp(clean_path, "/library/test/success.html") == 0) &&
                         !wifi_manager_sta_is_connected() && wifi_manager_get_fake_internet()) {
                    const char apple_ok[] = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
                    send_http_response(client_fd, 200, "text/html", apple_ok, sizeof(apple_ok) - 1);
                }
                /* Dashboard & API endpoints */
                else if (strcmp(clean_path, "/") == 0 || strcmp(clean_path, "/index.html") == 0) {
                    send_http_response(client_fd, 200, "text/html",
                                       ESPIRATE_DASHBOARD_HTML, sizeof(ESPIRATE_DASHBOARD_HTML) - 1);
                } else if (strcmp(clean_path, "/api/wifi") == 0) {
                    handle_api_wifi_get(client_fd);
                } else if (strcmp(clean_path, "/api/status") == 0) {
                    handle_api_status(client_fd);
                } else if (strcmp(clean_path, "/api/telemetry") == 0) {
                    handle_api_telemetry(client_fd);
                } else if (strcmp(clean_path, "/api/scripts") == 0) {
                    handle_api_scripts_list(client_fd);
                } else if (strcmp(clean_path, "/api/storage") == 0) {
                    handle_api_storage(client_fd);
                } else if (strncmp(clean_path, "/api/gpio", 9) == 0) {
                    handle_api_gpio_get(client_fd, req_path);
                } else if (strcmp(clean_path, "/api/pwm") == 0) {
                    handle_api_pwm_get(client_fd);
                } else if (strcmp(clean_path, "/api/i2c") == 0) {
                    handle_api_i2c_get(client_fd);
                } else if (strcmp(clean_path, "/api/spi") == 0) {
                    handle_api_spi_get(client_fd);
                } else if (strncmp(clean_path, "/api/script", 11) == 0) {
                    handle_api_get_script(client_fd, req_path);
                } else {
                    /* All other GET requests serve the dashboard so the user always sees the web page */
                    send_http_response(client_fd, 200, "text/html",
                                       ESPIRATE_DASHBOARD_HTML, sizeof(ESPIRATE_DASHBOARD_HTML) - 1);
                }
            } else if (strcmp(method, "POST") == 0) {
                if (strcmp(clean_path, "/api/wifi") == 0) {
                    handle_api_wifi_post(client_fd, body);
                } else if (strcmp(clean_path, "/api/wifi/mode") == 0) {
                    handle_api_wifi_mode(client_fd, body);
                } else if (strcmp(clean_path, "/api/wifi/config") == 0) {
                    handle_api_wifi_config(client_fd, body);
                } else if (strcmp(clean_path, "/api/wifi/forget") == 0) {
                    handle_api_wifi_forget(client_fd);
                } else if (strcmp(clean_path, "/api/gpio") == 0) {
                    handle_api_gpio_post(client_fd, body);
                } else if (strcmp(clean_path, "/api/pwm") == 0) {
                    handle_api_pwm_post(client_fd, body);
                } else if (strcmp(clean_path, "/api/i2c/scan") == 0) {
                    handle_api_i2c_scan(client_fd, body);
                } else if (strcmp(clean_path, "/api/i2c/write") == 0) {
                    handle_api_i2c_write(client_fd, body);
                } else if (strcmp(clean_path, "/api/i2c/read") == 0) {
                    handle_api_i2c_read(client_fd, body);
                } else if (strcmp(clean_path, "/api/spi") == 0 || strcmp(clean_path, "/api/spi/transfer") == 0) {
                    handle_api_spi_transfer(client_fd, body);
                } else if (strcmp(clean_path, "/api/telemetry/reset") == 0) {
                    espirate_telemetry_reset();
                    send_http_response(client_fd, 200, "application/json", "{\"status\":\"ok\"}", 15);
                } else if (strcmp(clean_path, "/api/reset") == 0) {
                    lua_worker_reset();
                    send_http_response(client_fd, 200, "application/json", "{\"status\":\"reset\"}", 18);
                } else if (strcmp(clean_path, "/api/script") == 0) {
                    handle_api_save_script(client_fd, body);
                } else if (strcmp(clean_path, "/api/script/delete") == 0 || strcmp(clean_path, "/api/file/delete") == 0) {
                    handle_api_delete_script(client_fd, body, req_path);
                } else if (strcmp(clean_path, "/api/script/rename") == 0 || strcmp(clean_path, "/api/file/rename") == 0) {
                    handle_api_rename_script(client_fd, body);
                } else if (strcmp(clean_path, "/api/storage/format") == 0) {
                    handle_api_storage_format(client_fd);
                } else if (strcmp(clean_path, "/api/run") == 0) {
                    handle_api_run(client_fd, body);
                } else {
                    send_http_response(client_fd, 404, "text/plain", "Not Found", 9);
                }
            } else if (strcmp(method, "DELETE") == 0) {
                if (strncmp(clean_path, "/api/script", 11) == 0 || strncmp(clean_path, "/api/file", 9) == 0) {
                    handle_api_delete_script(client_fd, body, req_path);
                } else {
                    send_http_response(client_fd, 404, "text/plain", "Not Found", 9);
                }
            } else {
                send_http_response(client_fd, 405, "text/plain", "Method Not Allowed", 18);
            }
        }

        zsock_close(client_fd);
    }
}

int web_server_init(void)
{
    k_thread_create(&web_server_thread_data,
                    web_server_stack,
                    K_THREAD_STACK_SIZEOF(web_server_stack),
                    web_server_thread_fn,
                    NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&web_server_thread_data, "web_server");
    return 0;
}

bool web_server_is_running(void)
{
    return s_running;
}
