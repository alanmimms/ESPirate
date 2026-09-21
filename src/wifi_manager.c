/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/igmp.h>
#include <zephyr/net/socket.h>
#include <zephyr/fs/fs.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#include "wifi_manager.h"
#include "captive_dns.h"
#include "fs_manager.h"

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

#define NET_EVENT_WIFI_ALL_MASK \
    (NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT | \
     NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED | \
     NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT | \
     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE)

#define NET_EVENT_IPV4_ALL_MASK \
    (NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_DHCP_BOUND)

#define WIFI_STA_FILE      "/lfs/.wifi_sta.dat"
#define WIFI_MODE_FILE     "/lfs/.wifi_mode"
#define WIFI_AP_SSID_FILE  "/lfs/.wifi_ap_ssid"
#define FAKE_INTERNET_FILE "/lfs/.fake_internet"
#define WIFI_STA_MAGIC     0x57465354 /* 'WFST' */

struct wifi_sta_record {
    uint32_t magic;
    char     ssid[33];
    char     pass[65];
    char     security[16];
    uint32_t checksum;
};

struct wifi_sta_record_v1 {
    uint32_t magic;
    char     ssid[33];
    char     pass[65];
    uint32_t checksum;
};

static struct net_if *s_iface = NULL;
static struct wifi_connect_req_params s_ap_config;
static struct wifi_connect_req_params s_sta_config;
static struct net_mgmt_event_callback s_wifi_mgmt_cb;
static struct net_mgmt_event_callback s_ipv4_mgmt_cb;

#define STA_MAX_RETRIES          3
#define STA_WATCHDOG_TIMEOUT_SEC 20
#define STA_RETRY_DELAY_SEC      2

static struct k_work_delayable s_sta_watchdog;
static struct k_work_delayable s_sta_retry_work;
static uint32_t s_sta_retry_count = 0;
static bool s_sta_connecting = false;
static bool s_sta_retry_pending = false;

static uint8_t s_mac[6] = {0};
static char s_hostname[32] = "espirate";
static char s_mdns_domain[40] = "espirate.local";

static espirate_wifi_mode_t s_active_mode = ESPIRATE_WIFI_MODE_AP;
static espirate_wifi_mode_t s_configured_mode = ESPIRATE_WIFI_MODE_AP;

static bool s_ap_active = false;
static bool s_dhcp_srv_active = false;
static char s_ap_ssid[33] = "ESPirate-AP";
static char s_ap_ip[16] = ESPIRATE_DEFAULT_IP;
static uint32_t s_sta_clients = 0;

static bool s_sta_connected = false;
static char s_sta_ssid[33] = {0};
static char s_sta_pass[65] = {0};
static char s_sta_sec_str[16] = "wpa2";
static enum wifi_security_type s_sta_security = WIFI_SECURITY_TYPE_PSK;
static char s_sta_ip[16] = "0.0.0.0";
static bool s_fake_internet = false;

static bool s_scan_in_progress = false;
static uint32_t s_scan_count = 0;

/* Forward declarations */
static int start_ap_mode(void);
static int start_sta_mode(void);
static int stop_ap_mode(void);
static int stop_sta_mode(void);
static int start_dhcpv4_server(void);
static void sta_watchdog_handler(struct k_work *work);
static void sta_retry_work_handler(struct k_work *work);
static void trigger_sta_retry(void);
static void fallback_to_ap_mode(void);
static enum wifi_security_type parse_security_str(const char *sec_str);

/* Obfuscate / de-obfuscate credentials using MAC-derived keystream */
static void crypt_record(struct wifi_sta_record *rec)
{
    uint8_t *p = (uint8_t *)rec;
    for (size_t i = 4; i < sizeof(struct wifi_sta_record) - 4; i++) {
        uint8_t k = s_mac[i % 6] ^ (uint8_t)(0x5A + (i * 37));
        p[i] ^= k;
    }
}

static void crypt_record_v1(struct wifi_sta_record_v1 *rec)
{
    uint8_t *p = (uint8_t *)rec;
    for (size_t i = 4; i < sizeof(struct wifi_sta_record_v1) - 4; i++) {
        uint8_t k = s_mac[i % 6] ^ (uint8_t)(0x5A + (i * 37));
        p[i] ^= k;
    }
}

static uint32_t calc_checksum(const struct wifi_sta_record *rec)
{
    uint32_t sum = 0x12345678;
    for (size_t i = 0; i < sizeof(rec->ssid); i++) {
        sum = (sum * 31) + (uint8_t)rec->ssid[i];
    }
    for (size_t i = 0; i < sizeof(rec->pass); i++) {
        sum = (sum * 37) + (uint8_t)rec->pass[i];
    }
    for (size_t i = 0; i < sizeof(rec->security); i++) {
        sum = (sum * 41) + (uint8_t)rec->security[i];
    }
    return sum;
}

static uint32_t calc_checksum_v1(const struct wifi_sta_record_v1 *rec)
{
    uint32_t sum = 0x12345678;
    for (size_t i = 0; i < sizeof(rec->ssid); i++) {
        sum = (sum * 31) + (uint8_t)rec->ssid[i];
    }
    for (size_t i = 0; i < sizeof(rec->pass); i++) {
        sum = (sum * 37) + (uint8_t)rec->pass[i];
    }
    return sum;
}

static int save_credentials_safely(const char *ssid, const char *password, const char *security)
{
    struct wifi_sta_record rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = WIFI_STA_MAGIC;
    strncpy(rec.ssid, ssid ? ssid : "", sizeof(rec.ssid) - 1);
    strncpy(rec.pass, password ? password : "", sizeof(rec.pass) - 1);
    strncpy(rec.security, security ? security : "wpa2", sizeof(rec.security) - 1);
    rec.checksum = calc_checksum(&rec);

    crypt_record(&rec);

    struct fs_file_t file;
    fs_file_t_init(&file);
    int ret = fs_open(&file, WIFI_STA_FILE, FS_O_CREATE | FS_O_WRITE);
    if (ret != 0) {
        LOG_ERR("Failed to open %s for write: %d", WIFI_STA_FILE, ret);
        return ret;
    }

    ssize_t written = fs_write(&file, &rec, sizeof(rec));
    fs_close(&file);

    if (written != sizeof(rec)) {
        LOG_ERR("Failed to write complete credentials");
        return -EIO;
    }

    LOG_INF("Station credentials saved securely into %s", WIFI_STA_FILE);
    return 0;
}

static enum wifi_security_type parse_security_str(const char *sec_str)
{
    if (!sec_str || strlen(sec_str) == 0) {
        return WIFI_SECURITY_TYPE_PSK;
    }
    if (strcasecmp(sec_str, "wpa3") == 0 || strcasecmp(sec_str, "sae") == 0) {
        return WIFI_SECURITY_TYPE_SAE;
    }
    if (strcasecmp(sec_str, "open") == 0 || strcasecmp(sec_str, "none") == 0) {
        return WIFI_SECURITY_TYPE_NONE;
    }
    return WIFI_SECURITY_TYPE_PSK;
}

static int load_credentials_safely(char *out_ssid, size_t ssid_len,
                                   char *out_pass, size_t pass_len,
                                   char *out_sec, size_t sec_len)
{
    struct fs_dirent entry;
    if (fs_stat(WIFI_STA_FILE, &entry) != 0) {
        return -ENOENT;
    }

    struct fs_file_t file;
    fs_file_t_init(&file);
    int ret = fs_open(&file, WIFI_STA_FILE, FS_O_READ);
    if (ret != 0) {
        return -ENOENT;
    }

    uint8_t raw[128] = {0};
    ssize_t read_bytes = fs_read(&file, raw, sizeof(raw));
    fs_close(&file);

    if (read_bytes == sizeof(struct wifi_sta_record)) {
        struct wifi_sta_record *rec = (struct wifi_sta_record *)raw;
        if (rec->magic != WIFI_STA_MAGIC) return -EINVAL;
        crypt_record(rec);
        if (rec->checksum != calc_checksum(rec)) {
            LOG_WRN("Credentials checksum mismatch (device or key changed)");
            return -EINVAL;
        }
        if (out_ssid && ssid_len > 0) {
            strncpy(out_ssid, rec->ssid, ssid_len - 1);
            out_ssid[ssid_len - 1] = '\0';
        }
        if (out_pass && pass_len > 0) {
            strncpy(out_pass, rec->pass, pass_len - 1);
            out_pass[pass_len - 1] = '\0';
        }
        if (out_sec && sec_len > 0) {
            strncpy(out_sec, rec->security, sec_len - 1);
            out_sec[sec_len - 1] = '\0';
        }
        return 0;
    } else if (read_bytes == sizeof(struct wifi_sta_record_v1)) {
        struct wifi_sta_record_v1 *rec1 = (struct wifi_sta_record_v1 *)raw;
        if (rec1->magic != WIFI_STA_MAGIC) return -EINVAL;
        crypt_record_v1(rec1);
        if (rec1->checksum != calc_checksum_v1(rec1)) {
            LOG_WRN("Credentials v1 checksum mismatch");
            return -EINVAL;
        }
        if (out_ssid && ssid_len > 0) {
            strncpy(out_ssid, rec1->ssid, ssid_len - 1);
            out_ssid[ssid_len - 1] = '\0';
        }
        if (out_pass && pass_len > 0) {
            strncpy(out_pass, rec1->pass, pass_len - 1);
            out_pass[pass_len - 1] = '\0';
        }
        if (out_sec && sec_len > 0) {
            strncpy(out_sec, "wpa2", sec_len - 1);
            out_sec[sec_len - 1] = '\0';
        }
        return 0;
    }

    return -EINVAL;
}

static void load_fake_internet_flag(void)
{
    struct fs_dirent entry;
    if (fs_stat(FAKE_INTERNET_FILE, &entry) != 0) {
        s_fake_internet = false;
        return;
    }

    struct fs_file_t file;
    fs_file_t_init(&file);
    if (fs_open(&file, FAKE_INTERNET_FILE, FS_O_READ) == 0) {
        char val = '0';
        fs_read(&file, &val, 1);
        fs_close(&file);
        s_fake_internet = (val == '1');
    } else {
        s_fake_internet = false;
    }
}

static void save_fake_internet_flag(bool enable)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    if (fs_open(&file, FAKE_INTERNET_FILE, FS_O_CREATE | FS_O_WRITE) == 0) {
        char val = enable ? '1' : '0';
        fs_write(&file, &val, 1);
        fs_close(&file);
    }
}

static void save_ap_ssid(const char *ssid)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    if (fs_open(&file, WIFI_AP_SSID_FILE, FS_O_CREATE | FS_O_WRITE) == 0) {
        fs_write(&file, ssid, strlen(ssid));
        fs_close(&file);
    }
}

static void load_ap_ssid(void)
{
    char old_default[33];
    snprintf(old_default, sizeof(old_default), "ESPirate%02X%02X", s_mac[4], s_mac[5]);

    /* Default AP SSID based on MAC: ESPirate-XXYY */
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "ESPirate-%02X%02X", s_mac[4], s_mac[5]);

    struct fs_dirent entry;
    if (fs_stat(WIFI_AP_SSID_FILE, &entry) == 0) {
        struct fs_file_t file;
        fs_file_t_init(&file);
        if (fs_open(&file, WIFI_AP_SSID_FILE, FS_O_READ) == 0) {
            char buf[33] = {0};
            ssize_t r = fs_read(&file, buf, sizeof(buf) - 1);
            fs_close(&file);
            if (r > 0) {
                buf[r] = '\0';
                /* If saved file matches legacy non-hyphenated default, upgrade it */
                if (strcmp(buf, old_default) == 0) {
                    save_ap_ssid(s_ap_ssid);
                } else {
                    strncpy(s_ap_ssid, buf, sizeof(s_ap_ssid) - 1);
                }
            }
        }
    }
}

static void load_configured_mode(void)
{
    s_configured_mode = ESPIRATE_WIFI_MODE_AP; /* Default AP */

    struct fs_dirent entry;
    if (fs_stat(WIFI_MODE_FILE, &entry) == 0) {
        struct fs_file_t file;
        fs_file_t_init(&file);
        if (fs_open(&file, WIFI_MODE_FILE, FS_O_READ) == 0) {
            char mode_ch = '0';
            fs_read(&file, &mode_ch, 1);
            fs_close(&file);
            if (mode_ch == '1') {
                s_configured_mode = ESPIRATE_WIFI_MODE_STA;
            }
        }
    }
}

static void save_configured_mode(espirate_wifi_mode_t mode)
{
    s_configured_mode = mode;
    struct fs_file_t file;
    fs_file_t_init(&file);
    if (fs_open(&file, WIFI_MODE_FILE, FS_O_CREATE | FS_O_WRITE) == 0) {
        char ch = (mode == ESPIRATE_WIFI_MODE_STA) ? '1' : '0';
        fs_write(&file, &ch, 1);
        fs_close(&file);
    }
}

static void fallback_to_ap_mode(void)
{
    s_sta_connecting = false;
    s_sta_connected = false;
    s_sta_retry_pending = false;
    k_work_cancel_delayable(&s_sta_watchdog);
    k_work_cancel_delayable(&s_sta_retry_work);
    stop_sta_mode();
    start_ap_mode();
}

static void trigger_sta_retry(void)
{
    if (s_active_mode != ESPIRATE_WIFI_MODE_STA || !s_sta_connecting || s_sta_connected) {
        return;
    }

    if (s_sta_retry_pending) {
        return;
    }

    if (s_sta_retry_count < STA_MAX_RETRIES) {
        s_sta_retry_count++;
        s_sta_retry_pending = true;
        LOG_WRN("Wi-Fi Station connection attempt failed. Retrying (%u/%u) in %d seconds...",
                s_sta_retry_count, STA_MAX_RETRIES, STA_RETRY_DELAY_SEC);
        k_work_reschedule(&s_sta_retry_work, K_SECONDS(STA_RETRY_DELAY_SEC));
        k_work_reschedule(&s_sta_watchdog, K_SECONDS(STA_WATCHDOG_TIMEOUT_SEC + STA_RETRY_DELAY_SEC));
    } else {
        LOG_WRN("Wi-Fi Station connection failed after %u attempts. Falling back to Soft-AP mode (%s)...",
                STA_MAX_RETRIES, s_ap_ssid);
        fallback_to_ap_mode();
    }
}

static void sta_retry_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    s_sta_retry_pending = false;

    if (s_active_mode != ESPIRATE_WIFI_MODE_STA || !s_sta_connecting || s_sta_connected) {
        return;
    }

    LOG_INF("Retrying Station connection to '%s' (attempt %u/%u, security: %s)...",
            s_sta_ssid, s_sta_retry_count, STA_MAX_RETRIES, s_sta_sec_str);

    /* Disconnect previous attempt cleanly to avoid -EALREADY (-120) */
    net_dhcpv4_stop(s_iface);
    net_mgmt(NET_REQUEST_WIFI_DISCONNECT, s_iface, NULL, 0);
    k_msleep(150);

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, s_iface, &s_sta_config, sizeof(s_sta_config));
    if (ret != 0) {
        LOG_WRN("NET_REQUEST_WIFI_CONNECT retry failed immediately: %d", ret);
        trigger_sta_retry();
    }
}

static void sta_watchdog_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (s_active_mode == ESPIRATE_WIFI_MODE_STA && s_sta_connecting && !s_sta_connected) {
        if (s_sta_retry_pending) {
            return;
        }
        LOG_WRN("Station connection attempt (%u/%u) timed out (%ds).",
                s_sta_retry_count, STA_MAX_RETRIES, STA_WATCHDOG_TIMEOUT_SEC);
        trigger_sta_retry();
    }
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
    ARG_UNUSED(iface);

    switch (mgmt_event) {
    case NET_EVENT_WIFI_AP_ENABLE_RESULT:
        LOG_INF("Wi-Fi Soft-AP '%s' enabled and broadcasting.", s_ap_ssid);
        s_ap_active = true;
        if (!s_dhcp_srv_active) {
            start_dhcpv4_server();
        }
        break;

    case NET_EVENT_WIFI_AP_DISABLE_RESULT:
        LOG_INF("Wi-Fi Soft-AP disabled.");
        s_ap_active = false;
        s_sta_clients = 0;
        break;

    case NET_EVENT_WIFI_AP_STA_CONNECTED: {
        struct wifi_ap_sta_info *info = (struct wifi_ap_sta_info *)cb->info;
        s_sta_clients++;
        if (info) {
            LOG_INF("AP Station joined: %02X:%02X:%02X:%02X:%02X:%02X (clients: %u)",
                    info->mac[0], info->mac[1], info->mac[2],
                    info->mac[3], info->mac[4], info->mac[5], s_sta_clients);
        } else {
            LOG_INF("AP Station joined (clients: %u)", s_sta_clients);
        }
        break;
    }

    case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
        struct wifi_ap_sta_info *info = (struct wifi_ap_sta_info *)cb->info;
        if (s_sta_clients > 0) {
            s_sta_clients--;
        }
        if (info) {
            LOG_INF("AP Station left: %02X:%02X:%02X:%02X:%02X:%02X (clients: %u)",
                    info->mac[0], info->mac[1], info->mac[2],
                    info->mac[3], info->mac[4], info->mac[5], s_sta_clients);
        } else {
            LOG_INF("AP Station left (clients: %u)", s_sta_clients);
        }
        break;
    }

    case NET_EVENT_WIFI_CONNECT_RESULT: {
        const struct wifi_status *status = (const struct wifi_status *)cb->info;
        int st = status ? status->status : -1;
        if (st == 0) {
            LOG_INF("Wi-Fi Station 802.11 association & handshake SUCCESS with '%s'! Requesting DHCPv4 lease...", s_sta_ssid);
            net_dhcpv4_start(s_iface);
            k_work_reschedule(&s_sta_watchdog, K_SECONDS(STA_WATCHDOG_TIMEOUT_SEC));
        } else {
            LOG_WRN("Wi-Fi Station connect FAILED: status=%d (%s) for SSID '%s'",
                    st, wifi_conn_status_txt(st), s_sta_ssid);
            if (s_active_mode == ESPIRATE_WIFI_MODE_STA && s_sta_connecting && !s_sta_connected) {
                trigger_sta_retry();
            }
        }
        break;
    }

    case NET_EVENT_WIFI_DISCONNECT_RESULT: {
        const struct wifi_status *status = (const struct wifi_status *)cb->info;
        int st = status ? status->status : 0;
        LOG_WRN("Wi-Fi Station DISCONNECTED: status=%d (%s)", st, wifi_conn_status_txt(st));
        net_dhcpv4_stop(s_iface);
        bool was_connected = s_sta_connected;
        s_sta_connected = false;
        strcpy(s_sta_ip, "0.0.0.0");
        if (s_active_mode == ESPIRATE_WIFI_MODE_STA) {
            if (was_connected) {
                LOG_WRN("Wi-Fi Station lost connection to '%s'. Starting reconnect sequence...", s_sta_ssid);
                s_sta_connecting = true;
                s_sta_retry_count = 0;
                s_sta_retry_pending = false;
                trigger_sta_retry();
            } else if (s_sta_connecting) {
                trigger_sta_retry();
            }
        }
        break;
    }

    case NET_EVENT_WIFI_SCAN_RESULT: {
        const struct wifi_scan_result *res = (const struct wifi_scan_result *)cb->info;
        if (res) {
            s_scan_count++;
            char bssid_str[18] = {0};
            snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     res->mac[0], res->mac[1], res->mac[2],
                     res->mac[3], res->mac[4], res->mac[5]);
            LOG_INF("  Scan [%2u]: SSID '%-32s' | Ch:%2u | RSSI:%4d dBm | Sec:%-12s | BSSID:%s",
                    s_scan_count, res->ssid, res->channel, res->rssi,
                    wifi_security_txt(res->security), bssid_str);
        }
        break;
    }

    case NET_EVENT_WIFI_SCAN_DONE: {
        const struct wifi_status *status = (const struct wifi_status *)cb->info;
        int st = status ? status->status : 0;
        LOG_INF("Wi-Fi Scan completed: %u networks discovered (status=%d)", s_scan_count, st);
        s_scan_in_progress = false;
        if (s_active_mode == ESPIRATE_WIFI_MODE_AP && !s_ap_active) {
            start_ap_mode();
        }
        break;
    }

    default:
        break;
    }
}

static void send_mdns_announcement(const char *hostname, struct in_addr *ip, uint32_t ttl)
{
    if (!hostname || !ip || strlen(hostname) == 0) {
        return;
    }

    int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return;
    }

    uint8_t pkt[128];
    uint8_t *p = pkt;

    /* DNS Header */
    *p++ = 0x00; *p++ = 0x00; /* ID = 0 */
    *p++ = 0x84; *p++ = 0x00; /* Flags: Response (0x80), Authoritative (0x04) */
    *p++ = 0x00; *p++ = 0x00; /* QDCOUNT = 0 */
    *p++ = 0x00; *p++ = 0x01; /* ANCOUNT = 1 */
    *p++ = 0x00; *p++ = 0x00; /* NSCOUNT = 0 */
    *p++ = 0x00; *p++ = 0x00; /* ARCOUNT = 0 */

    /* Answer Name: <hostname>.local */
    size_t hlen = strlen(hostname);
    if (hlen > 63) hlen = 63;
    *p++ = (uint8_t)hlen;
    memcpy(p, hostname, hlen);
    p += hlen;

    *p++ = 0x05; /* length of "local" */
    memcpy(p, "local", 5);
    p += 5;
    *p++ = 0x00; /* null terminator */

    /* Type A (0x0001) */
    *p++ = 0x00; *p++ = 0x01;

    /* Class IN (0x0001) | Cache-Flush bit (0x8000) = 0x8001 */
    *p++ = 0x80; *p++ = 0x01;

    /* TTL (4 bytes, big-endian) */
    *p++ = (uint8_t)((ttl >> 24) & 0xFF);
    *p++ = (uint8_t)((ttl >> 16) & 0xFF);
    *p++ = (uint8_t)((ttl >> 8) & 0xFF);
    *p++ = (uint8_t)(ttl & 0xFF);

    /* RDLENGTH = 4 */
    *p++ = 0x00; *p++ = 0x04;

    /* RDATA (IPv4 Address bytes) */
    memcpy(p, &ip->s_addr, 4);
    p += 4;

    size_t pkt_len = p - pkt;

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(5353);
    net_addr_pton(AF_INET, "224.0.0.251", &dest.sin_addr);

    /* Send two consecutive announcements separated by 50ms per RFC 6762 */
    zsock_sendto(sock, pkt, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest));
    k_msleep(50);
    zsock_sendto(sock, pkt, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest));

    zsock_close(sock);
    char ip_str[NET_IPV4_ADDR_LEN] = {0};
    net_addr_ntop(AF_INET, ip, ip_str, sizeof(ip_str));
    LOG_INF("Sent mDNS announcement: %s.local -> %s (TTL=%u, cache-flush)",
            hostname, ip_str, ttl);
}

static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);

    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD || mgmt_event == NET_EVENT_IPV4_DHCP_BOUND) {
        if (s_active_mode == ESPIRATE_WIFI_MODE_STA && s_iface && s_iface->config.ip.ipv4) {
            for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
                if (s_iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
                    char buf[NET_IPV4_ADDR_LEN];
                    net_addr_ntop(AF_INET,
                                  &s_iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
                                  buf, sizeof(buf));
                    if (strcmp(buf, "192.168.4.1") != 0 && strcmp(buf, "0.0.0.0") != 0) {
                        strncpy(s_sta_ip, buf, sizeof(s_sta_ip) - 1);
                        s_sta_connected = true;
                        s_sta_connecting = false;
                        s_sta_retry_count = 0;
                        s_sta_retry_pending = false;
                        k_work_cancel_delayable(&s_sta_watchdog);
                        k_work_cancel_delayable(&s_sta_retry_work);
                        LOG_INF("Station DHCP bound! IP Address: %s (mDNS: %s)",
                                s_sta_ip, s_mdns_domain);
                        /* Remove lingering AP IP if still on interface */
                        struct in_addr ap_old_addr;
                        if (net_addr_pton(AF_INET, ESPIRATE_DEFAULT_IP, &ap_old_addr) == 0) {
                            net_if_ipv4_addr_rm(s_iface, &ap_old_addr);
                        }
                        struct in_addr mdns_mcast;
                        net_addr_pton(AF_INET, "224.0.0.251", &mdns_mcast);
                        int igmp_ret = net_ipv4_igmp_join(s_iface, &mdns_mcast, NULL);
                        LOG_INF("Joined mDNS multicast group 224.0.0.251 (ret=%d)", igmp_ret);

                        /* Send unsolicited mDNS announcement with cache-flush bit to update all peers */
                        struct in_addr sta_ip_addr;
                        if (net_addr_pton(AF_INET, s_sta_ip, &sta_ip_addr) == 0) {
                            send_mdns_announcement(s_hostname, &sta_ip_addr, 15);
                        }
                        break;
                    }
                }
            }
        }
    }
}

static int start_dhcpv4_server(void)
{
    if (!s_iface) {
        return -ENODEV;
    }

    struct in_addr addr;
    struct in_addr netmask;

    net_addr_pton(AF_INET, s_ap_ip, &addr);
    net_addr_pton(AF_INET, ESPIRATE_DEFAULT_MASK, &netmask);

    net_if_ipv4_set_gw(s_iface, &addr);

    if (net_if_ipv4_addr_add(s_iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
        LOG_DBG("AP IPv4 address already assigned or manual");
    }

    if (!net_if_ipv4_set_netmask_by_addr(s_iface, &addr, &netmask)) {
        LOG_ERR("Failed to set netmask for AP interface");
        return -EINVAL;
    }

    struct in_addr pool_start = addr;
    pool_start.s4_addr[3] = 10;

    int ret = net_dhcpv4_server_start(s_iface, &pool_start);
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("DHCPv4 server failed to start: %d", ret);
        return ret;
    }

    s_dhcp_srv_active = true;
    LOG_INF("DHCPv4 server active at %s (Router/DNS: %s, Pool: .10 - .50)",
            s_ap_ip, s_ap_ip);
    return 0;
}

static int start_ap_mode(void)
{
    if (!s_iface) {
        s_iface = net_if_get_default();
        if (!s_iface) return -ENODEV;
    }

    LOG_INF("Starting Soft-AP Mode '%s'...", s_ap_ssid);

    /* Clean up any existing unicast addresses before assigning AP IP */
    if (s_iface && s_iface->config.ip.ipv4) {
        for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
            if (s_iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
                net_if_ipv4_addr_rm(s_iface, &s_iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr);
            }
        }
    }

    memset(&s_ap_config, 0, sizeof(s_ap_config));
    s_ap_config.ssid = (const uint8_t *)s_ap_ssid;
    s_ap_config.ssid_length = strlen(s_ap_ssid);
    s_ap_config.channel = 1;
    s_ap_config.band = WIFI_FREQ_BAND_2_4_GHZ;
    s_ap_config.security = WIFI_SECURITY_TYPE_NONE;

    /* Assign AP IP before starting AP */
    struct in_addr addr, netmask;
    net_addr_pton(AF_INET, s_ap_ip, &addr);
    net_addr_pton(AF_INET, ESPIRATE_DEFAULT_MASK, &netmask);
    net_if_ipv4_set_gw(s_iface, &addr);
    net_if_ipv4_addr_add(s_iface, &addr, NET_ADDR_MANUAL, 0);
    net_if_ipv4_set_netmask_by_addr(s_iface, &addr, &netmask);

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, s_iface, &s_ap_config, sizeof(s_ap_config));
    if (ret != 0) {
        LOG_ERR("NET_REQUEST_WIFI_AP_ENABLE failed: %d", ret);
        return ret;
    }

    s_active_mode = ESPIRATE_WIFI_MODE_AP;
    s_ap_active = true;
    captive_dns_set_enabled(true);

    struct in_addr ap_ip_addr;
    if (net_addr_pton(AF_INET, s_ap_ip, &ap_ip_addr) == 0) {
        send_mdns_announcement(s_hostname, &ap_ip_addr, 15);
    }

    return 0;
}

static int stop_ap_mode(void)
{
    if (!s_iface || !s_ap_active) {
        return 0;
    }

    LOG_INF("Stopping Soft-AP Mode '%s'...", s_ap_ssid);

    /* Send goodbye packet with TTL=0 to purge 192.168.4.1 from peer mDNS caches */
    struct in_addr old_ap_ip;
    if (net_addr_pton(AF_INET, s_ap_ip, &old_ap_ip) == 0) {
        send_mdns_announcement(s_hostname, &old_ap_ip, 0);
    }

    if (s_dhcp_srv_active) {
        net_dhcpv4_server_stop(s_iface);
        s_dhcp_srv_active = false;
    }

    /* Clean up and mark overridable any AP unicast addresses on interface */
    if (s_iface && s_iface->config.ip.ipv4) {
        for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
            if (s_iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
                s_iface->config.ip.ipv4->unicast[i].ipv4.addr_type = NET_ADDR_OVERRIDABLE;
                net_if_ipv4_addr_rm(s_iface, &s_iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr);
            }
        }
        struct in_addr zero_gw = {0};
        net_if_ipv4_set_gw(s_iface, &zero_gw);
    }

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, s_iface, NULL, 0);
    s_ap_active = false;
    s_sta_clients = 0;
    captive_dns_set_enabled(false);

    return ret;
}

static int start_sta_mode(void)
{
    if (!s_iface) {
        s_iface = net_if_get_default();
        if (!s_iface) return -ENODEV;
    }

    char saved_ssid[33] = {0};
    char saved_pass[65] = {0};
    char saved_sec[16] = {0};
    if (load_credentials_safely(saved_ssid, sizeof(saved_ssid),
                                saved_pass, sizeof(saved_pass),
                                saved_sec, sizeof(saved_sec)) != 0 ||
        strlen(saved_ssid) == 0) {
        LOG_WRN("No saved Station credentials. Remaining in AP mode.");
        return -ENOENT;
    }

    strncpy(s_sta_ssid, saved_ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';
    strncpy(s_sta_pass, saved_pass, sizeof(s_sta_pass) - 1);
    s_sta_pass[sizeof(s_sta_pass) - 1] = '\0';
    if (strlen(saved_sec) > 0) {
        strncpy(s_sta_sec_str, saved_sec, sizeof(s_sta_sec_str) - 1);
        s_sta_sec_str[sizeof(s_sta_sec_str) - 1] = '\0';
    }
    s_sta_security = parse_security_str(s_sta_sec_str);

    s_sta_retry_count = 1;
    s_sta_connecting = true;
    s_sta_connected = false;
    s_sta_retry_pending = false;
    strcpy(s_sta_ip, "0.0.0.0");
    s_active_mode = ESPIRATE_WIFI_MODE_STA;

    memset(&s_sta_config, 0, sizeof(s_sta_config));
    s_sta_config.ssid = (const uint8_t *)s_sta_ssid;
    s_sta_config.ssid_length = strlen(s_sta_ssid);
    s_sta_config.channel = WIFI_CHANNEL_ANY;
    s_sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;
    s_sta_config.mfp = WIFI_MFP_OPTIONAL;
    s_sta_config.security = s_sta_security;

    if (s_sta_security != WIFI_SECURITY_TYPE_NONE && strlen(s_sta_pass) > 0) {
        s_sta_config.psk = (const uint8_t *)s_sta_pass;
        s_sta_config.psk_length = strlen(s_sta_pass);
        s_sta_config.sae_password = (const uint8_t *)s_sta_pass;
        s_sta_config.sae_password_length = strlen(s_sta_pass);
    }

    LOG_INF("Starting Station Mode (attempt %u/%u):", s_sta_retry_count, STA_MAX_RETRIES);
    LOG_INF("  Target SSID : '%s' (length: %zu)", s_sta_ssid, strlen(s_sta_ssid));
    LOG_INF("  Password    : [%zu chars, masked]", strlen(s_sta_pass));
    LOG_INF("  Security    : %s (%s)", wifi_security_txt(s_sta_security), s_sta_sec_str);
    LOG_INF("  Band/Channel: 2.4 GHz, ANY (All-channel scan)");
    LOG_INF("  MFP (802.11w): OPTIONAL (Capable & Required per WPA3)");

    /* Ensure clean slate for station interface */
    net_dhcpv4_stop(s_iface);
    if (s_iface && s_iface->config.ip.ipv4) {
        for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
            if (s_iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
                s_iface->config.ip.ipv4->unicast[i].ipv4.addr_type = NET_ADDR_OVERRIDABLE;
                net_if_ipv4_addr_rm(s_iface, &s_iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr);
            }
        }
        struct in_addr zero_gw = {0};
        net_if_ipv4_set_gw(s_iface, &zero_gw);
    }

    /* Schedule watchdog for timeout */
    k_work_schedule(&s_sta_watchdog, K_SECONDS(STA_WATCHDOG_TIMEOUT_SEC));

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, s_iface, &s_sta_config, sizeof(s_sta_config));
    if (ret != 0) {
        LOG_ERR("NET_REQUEST_WIFI_CONNECT failed immediately with code: %d", ret);
        trigger_sta_retry();
        return ret;
    }

    LOG_INF("Connect request queued to ESP32 Wi-Fi driver successfully.");
    return 0;
}

static int stop_sta_mode(void)
{
    if (!s_iface) {
        return 0;
    }

    k_work_cancel_delayable(&s_sta_watchdog);
    k_work_cancel_delayable(&s_sta_retry_work);
    s_sta_connecting = false;
    s_sta_connected = false;
    s_sta_retry_pending = false;
    strcpy(s_sta_ip, "0.0.0.0");

    LOG_INF("Stopping Station Mode...");
    net_dhcpv4_stop(s_iface);
    int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, s_iface, NULL, 0);

    /* Explicitly stop ESP32 Wi-Fi driver so AP mode can start cleanly */
    esp_wifi_stop();

    /* Remove any unicast addresses on interface */
    if (s_iface && s_iface->config.ip.ipv4) {
        for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
            if (s_iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
                net_if_ipv4_addr_rm(s_iface, &s_iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr);
            }
        }
    }

    return ret;
}

int wifi_manager_init(void)
{
    /* Read hardware MAC and configure dynamic hostname espirate-xxyy */
    esp_read_mac(s_mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "espirate-%02x%02x", s_mac[4], s_mac[5]);
    snprintf(s_mdns_domain, sizeof(s_mdns_domain), "%s.local", s_hostname);

    net_hostname_set(s_hostname, strlen(s_hostname));
    LOG_INF("Hardware MAC: %02X:%02X:%02X:%02X:%02X:%02X | mDNS Hostname: %s (http://%s)",
            s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5],
            s_hostname, s_mdns_domain);

    k_work_init_delayable(&s_sta_watchdog, sta_watchdog_handler);
    k_work_init_delayable(&s_sta_retry_work, sta_retry_work_handler);

    net_mgmt_init_event_callback(&s_wifi_mgmt_cb, wifi_mgmt_event_handler, NET_EVENT_WIFI_ALL_MASK);
    net_mgmt_add_event_callback(&s_wifi_mgmt_cb);

    net_mgmt_init_event_callback(&s_ipv4_mgmt_cb, ipv4_mgmt_event_handler, NET_EVENT_IPV4_ALL_MASK);
    net_mgmt_add_event_callback(&s_ipv4_mgmt_cb);

    s_iface = net_if_get_default();
    if (!s_iface) {
        LOG_ERR("No default network interface found");
        return -ENODEV;
    }

    load_ap_ssid();
    load_fake_internet_flag();
    load_configured_mode();

    if (wifi_manager_has_saved_sta()) {
        char saved_sec[16] = {0};
        if (load_credentials_safely(s_sta_ssid, sizeof(s_sta_ssid),
                                    s_sta_pass, sizeof(s_sta_pass),
                                    saved_sec, sizeof(saved_sec)) == 0) {
            if (strlen(saved_sec) > 0) {
                strncpy(s_sta_sec_str, saved_sec, sizeof(s_sta_sec_str) - 1);
            }
            s_sta_security = parse_security_str(s_sta_sec_str);
        }
    }

    if (s_configured_mode == ESPIRATE_WIFI_MODE_STA && wifi_manager_has_saved_sta()) {
        int ret = start_sta_mode();
        if (ret != 0) {
            start_ap_mode();
        }
    } else {
        start_ap_mode();
    }

    return 0;
}

int wifi_manager_set_mode(espirate_wifi_mode_t mode, bool save)
{
    if (mode == s_active_mode && (mode == ESPIRATE_WIFI_MODE_AP || s_sta_connected)) {
        if (save) save_configured_mode(mode);
        return 0;
    }

    if (mode == ESPIRATE_WIFI_MODE_AP) {
        stop_sta_mode();
        k_msleep(100);
        int ret = start_ap_mode();
        if (ret == 0 && save) {
            save_configured_mode(ESPIRATE_WIFI_MODE_AP);
        }
        return ret;
    } else {
        if (!wifi_manager_has_saved_sta()) {
            LOG_ERR("Cannot switch to STA mode: no credentials configured");
            return -ENOENT;
        }
        if (s_ap_active) {
            stop_ap_mode();
            k_msleep(100);
        } else {
            stop_sta_mode();
            k_msleep(100);
        }
        int ret = start_sta_mode();
        if (ret == 0 && save) {
            save_configured_mode(ESPIRATE_WIFI_MODE_STA);
        }
        return ret;
    }
}

espirate_wifi_mode_t wifi_manager_get_active_mode(void)
{
    return s_active_mode;
}

espirate_wifi_mode_t wifi_manager_get_configured_mode(void)
{
    return s_configured_mode;
}

const char *wifi_manager_get_mode_str(void)
{
    return (s_active_mode == ESPIRATE_WIFI_MODE_STA) ? "STA" : "AP";
}

int wifi_manager_set_ap_ssid(const char *ssid, bool save)
{
    if (!ssid || strlen(ssid) == 0) return -EINVAL;
    strncpy(s_ap_ssid, ssid, sizeof(s_ap_ssid) - 1);
    s_ap_ssid[sizeof(s_ap_ssid) - 1] = '\0';
    if (save) save_ap_ssid(s_ap_ssid);

    if (s_active_mode == ESPIRATE_WIFI_MODE_AP && s_ap_active) {
        stop_ap_mode();
        start_ap_mode();
    }
    return 0;
}

const char *wifi_manager_get_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *wifi_manager_get_ap_ip(void)
{
    return s_ap_ip;
}

uint32_t wifi_manager_get_station_count(void)
{
    return s_sta_clients;
}

bool wifi_manager_is_ap_active(void)
{
    return s_ap_active;
}

const char *wifi_manager_get_ssid(void)
{
    return (s_active_mode == ESPIRATE_WIFI_MODE_STA) ? s_sta_ssid : s_ap_ssid;
}

const char *wifi_manager_get_ip(void)
{
    return (s_active_mode == ESPIRATE_WIFI_MODE_STA) ? s_sta_ip : s_ap_ip;
}

int wifi_manager_set_sta_credentials(const char *ssid, const char *password, const char *security, bool save)
{
    if (!ssid || strlen(ssid) == 0) return -EINVAL;
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';
    strncpy(s_sta_pass, password ? password : "", sizeof(s_sta_pass) - 1);
    s_sta_pass[sizeof(s_sta_pass) - 1] = '\0';

    if (security && strlen(security) > 0) {
        strncpy(s_sta_sec_str, security, sizeof(s_sta_sec_str) - 1);
        s_sta_sec_str[sizeof(s_sta_sec_str) - 1] = '\0';
    } else {
        strncpy(s_sta_sec_str, "wpa2", sizeof(s_sta_sec_str) - 1);
    }
    s_sta_security = parse_security_str(s_sta_sec_str);

    if (save) {
        save_credentials_safely(s_sta_ssid, s_sta_pass, s_sta_sec_str);
    }
    return 0;
}

const char *wifi_manager_get_sta_security_str(void)
{
    return s_sta_sec_str;
}

int wifi_manager_scan(void)
{
    if (!s_iface) {
        s_iface = net_if_get_default();
        if (!s_iface) return -ENODEV;
    }

    if (s_scan_in_progress) {
        LOG_WRN("Wi-Fi scan already in progress");
        return -EBUSY;
    }

    s_scan_count = 0;
    s_scan_in_progress = true;

    struct wifi_scan_params params = { 0 };
    params.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    params.bands = WIFI_FREQ_BAND_2_4_GHZ;

    LOG_INF("Starting Wi-Fi scan on all 2.4 GHz channels...");
    int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, s_iface, &params, sizeof(params));
    if (ret != 0) {
        LOG_ERR("NET_REQUEST_WIFI_SCAN failed: %d", ret);
        s_scan_in_progress = false;
        return ret;
    }
    return 0;
}

int wifi_manager_connect_sta(void)
{
    return wifi_manager_set_mode(ESPIRATE_WIFI_MODE_STA, true);
}

int wifi_manager_disconnect_sta(void)
{
    return wifi_manager_set_mode(ESPIRATE_WIFI_MODE_AP, false);
}

int wifi_manager_forget_sta(void)
{
    k_work_cancel_delayable(&s_sta_watchdog);
    k_work_cancel_delayable(&s_sta_retry_work);
    s_sta_connecting = false;
    s_sta_connected = false;
    s_sta_retry_pending = false;
    s_sta_ssid[0] = '\0';
    s_sta_pass[0] = '\0';
    strcpy(s_sta_sec_str, "wpa2");
    s_sta_security = WIFI_SECURITY_TYPE_PSK;
    fs_unlink(WIFI_STA_FILE);

    if (s_active_mode == ESPIRATE_WIFI_MODE_STA) {
        wifi_manager_set_mode(ESPIRATE_WIFI_MODE_AP, true);
    } else {
        save_configured_mode(ESPIRATE_WIFI_MODE_AP);
    }
    return 0;
}

bool wifi_manager_sta_is_connected(void)
{
    return (s_active_mode == ESPIRATE_WIFI_MODE_STA && s_sta_connected);
}

bool wifi_manager_has_saved_sta(void)
{
    char ssid[33] = {0};
    return (load_credentials_safely(ssid, sizeof(ssid), NULL, 0, NULL, 0) == 0 && strlen(ssid) > 0);
}

const char *wifi_manager_get_sta_ssid(void)
{
    if (strlen(s_sta_ssid) > 0) {
        return s_sta_ssid;
    }
    static char saved[33];
    if (load_credentials_safely(saved, sizeof(saved), NULL, 0, NULL, 0) == 0) {
        return saved;
    }
    return "";
}

const char *wifi_manager_get_sta_ip(void)
{
    return s_sta_ip;
}

uint32_t wifi_manager_get_sta_retry_count(void)
{
    return s_sta_retry_count;
}

uint32_t wifi_manager_get_sta_max_retries(void)
{
    return STA_MAX_RETRIES;
}

const char *wifi_manager_get_hostname(void)
{
    return s_hostname;
}

const char *wifi_manager_get_mdns_domain(void)
{
    return s_mdns_domain;
}

void wifi_manager_set_fake_internet(bool enable)
{
    s_fake_internet = enable;
    save_fake_internet_flag(enable);
    if (s_active_mode == ESPIRATE_WIFI_MODE_AP) {
        captive_dns_set_enabled(enable);
    }
    LOG_INF("Pretend Internet Connectivity set to: %s", enable ? "ENABLED" : "DISABLED");
}

bool wifi_manager_get_fake_internet(void)
{
    return s_fake_internet;
}
