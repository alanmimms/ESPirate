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

LOG_MODULE_REGISTER(web_server, LOG_LEVEL_INF);

#define WEB_SERVER_PORT 80
#define REQ_BUF_SIZE    4096

K_THREAD_STACK_DEFINE(web_server_stack, 8192);
static struct k_thread web_server_thread_data;
static bool s_running = false;

static void send_http_response(int sock, int status_code, const char *content_type,
                               const char *body, size_t body_len)
{
    char hdr[256];
    const char *status_str = "OK";
    if (status_code == 204) status_str = "No Content";
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

    zsock_send(sock, hdr, hdr_len, 0);
    if (body && body_len > 0) {
        zsock_send(sock, body, body_len, 0);
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
    char resp[1024];
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
    static char content[3072];
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

        ssize_t received = zsock_recv(client_fd, req_buf, sizeof(req_buf) - 1, 0);
        if (received > 0) {
            req_buf[received] = '\0';

            char method[8] = {0};
            char path[128] = {0};
            sscanf(req_buf, "%7s %127s", method, path);

            /* Locate HTTP body (after \r\n\r\n) */
            char *body = strstr(req_buf, "\r\n\r\n");
            if (body) {
                body += 4;
            } else {
                body = "";
            }

            if (strcmp(method, "GET") == 0) {
                /* Android 17 / Chrome 204 Probe */
                if (strcmp(path, "/generate_204") == 0 || strcmp(path, "/gen_204") == 0 ||
                    strstr(path, "generate_204") != NULL || strstr(path, "gen_204") != NULL) {
                    send_http_response(client_fd, 204, "text/plain", NULL, 0);
                }
                /* Apple Captive Portal Detection */
                else if (strcmp(path, "/hotspot-detect.html") == 0 ||
                         strcmp(path, "/library/test/success.html") == 0) {
                    const char apple_ok[] = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
                    send_http_response(client_fd, 200, "text/html", apple_ok, sizeof(apple_ok) - 1);
                }
                /* Microsoft NCSI Probes */
                else if (strcmp(path, "/ncsi.txt") == 0) {
                    send_http_response(client_fd, 200, "text/plain", "Microsoft NCSI", 14);
                } else if (strcmp(path, "/connecttest.txt") == 0) {
                    send_http_response(client_fd, 200, "text/plain", "Microsoft Connect Test", 22);
                }
                /* Dashboard & API endpoints */
                else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
                    send_http_response(client_fd, 200, "text/html",
                                       ESPIRATE_DASHBOARD_HTML, sizeof(ESPIRATE_DASHBOARD_HTML) - 1);
                } else if (strcmp(path, "/api/wifi") == 0) {
                    handle_api_wifi_get(client_fd);
                } else if (strcmp(path, "/api/status") == 0) {
                    handle_api_status(client_fd);
                } else if (strcmp(path, "/api/telemetry") == 0) {
                    handle_api_telemetry(client_fd);
                } else if (strcmp(path, "/api/scripts") == 0) {
                    handle_api_scripts_list(client_fd);
                } else if (strncmp(path, "/api/script?", 12) == 0) {
                    handle_api_get_script(client_fd, path);
                } else if (wifi_manager_get_fake_internet()) {
                    /* If fake internet is active, serve dashboard for any captive portal browser */
                    send_http_response(client_fd, 200, "text/html",
                                       ESPIRATE_DASHBOARD_HTML, sizeof(ESPIRATE_DASHBOARD_HTML) - 1);
                } else {
                    send_http_response(client_fd, 404, "text/plain", "Not Found", 9);
                }
            } else if (strcmp(method, "POST") == 0) {
                if (strcmp(path, "/api/wifi") == 0) {
                    handle_api_wifi_post(client_fd, body);
                } else if (strcmp(path, "/api/wifi/mode") == 0) {
                    handle_api_wifi_mode(client_fd, body);
                } else if (strcmp(path, "/api/wifi/config") == 0) {
                    handle_api_wifi_config(client_fd, body);
                } else if (strcmp(path, "/api/wifi/forget") == 0) {
                    handle_api_wifi_forget(client_fd);
                } else if (strcmp(path, "/api/telemetry/reset") == 0) {
                    espirate_telemetry_reset();
                    send_http_response(client_fd, 200, "application/json", "{\"status\":\"ok\"}", 15);
                } else if (strcmp(path, "/api/reset") == 0) {
                    lua_worker_reset();
                    send_http_response(client_fd, 200, "application/json", "{\"status\":\"reset\"}", 18);
                } else if (strcmp(path, "/api/script") == 0) {
                    handle_api_save_script(client_fd, body);
                } else if (strcmp(path, "/api/run") == 0) {
                    handle_api_run(client_fd, body);
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
