/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/hostname.h>
#include <zephyr/fs/fs.h>
#include <esp_mac.h>
#include <string.h>
#include <stdio.h>

#include "wifi_manager.h"
#include "captive_dns.h"
#include "fs_manager.h"

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

#define NET_EVENT_WIFI_ALL_MASK \
    (NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT | \
     NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED | \
     NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT | \
     NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_DHCP_BOUND)

#define WIFI_STA_FILE "/lfs/.wifi_sta.dat"
#define FAKE_INTERNET_FILE "/lfs/.fake_internet"
#define WIFI_STA_MAGIC 0x57465354 /* 'WFST' */

struct wifi_sta_record {
    uint32_t magic;
    char     ssid[33];
    char     pass[65];
    uint32_t checksum;
};

static struct net_if *s_ap_iface = NULL;
static struct net_if *s_sta_iface = NULL;
static struct wifi_connect_req_params s_ap_config;
static struct wifi_connect_req_params s_sta_config;
static struct net_mgmt_event_callback s_mgmt_cb;

static uint8_t s_mac[6] = {0};
static char s_hostname[32] = "espirate";
static char s_mdns_domain[40] = "espirate.local";

static bool s_ap_active = false;
static bool s_dhcp_srv_active = false;
static char s_ap_ssid[33] = ESPIRATE_DEFAULT_SSID;
static char s_ap_ip[16] = ESPIRATE_DEFAULT_IP;
static uint32_t s_sta_clients = 0;

static bool s_sta_connected = false;
static char s_sta_ssid[33] = {0};
static char s_sta_ip[16] = "0.0.0.0";
static bool s_fake_internet = true;

static int start_dhcpv4_server(void);

/* Obfuscate / de-obfuscate credentials using MAC-derived keystream */
static void crypt_record(struct wifi_sta_record *rec)
{
    uint8_t *p = (uint8_t *)rec;
    for (size_t i = 4; i < sizeof(struct wifi_sta_record) - 4; i++) {
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
    return sum;
}

static int save_credentials_safely(const char *ssid, const char *password)
{
    struct wifi_sta_record rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = WIFI_STA_MAGIC;
    strncpy(rec.ssid, ssid ? ssid : "", sizeof(rec.ssid) - 1);
    strncpy(rec.pass, password ? password : "", sizeof(rec.pass) - 1);
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

static int load_credentials_safely(char *out_ssid, size_t ssid_len,
                                   char *out_pass, size_t pass_len)
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

    struct wifi_sta_record rec;
    ssize_t read_bytes = fs_read(&file, &rec, sizeof(rec));
    fs_close(&file);

    if (read_bytes != sizeof(rec) || rec.magic != WIFI_STA_MAGIC) {
        return -EINVAL;
    }

    crypt_record(&rec);

    if (rec.checksum != calc_checksum(&rec)) {
        LOG_WRN("Credentials checksum mismatch (device or key changed)");
        return -EINVAL;
    }

    if (out_ssid) {
        strncpy(out_ssid, rec.ssid, ssid_len - 1);
        out_ssid[ssid_len - 1] = '\0';
    }
    if (out_pass) {
        strncpy(out_pass, rec.pass, pass_len - 1);
        out_pass[pass_len - 1] = '\0';
    }

    return 0;
}

static void load_fake_internet_flag(void)
{
    struct fs_dirent entry;
    if (fs_stat(FAKE_INTERNET_FILE, &entry) != 0) {
        s_fake_internet = true;
        captive_dns_set_enabled(true);
        return;
    }

    struct fs_file_t file;
    fs_file_t_init(&file);
    if (fs_open(&file, FAKE_INTERNET_FILE, FS_O_READ) == 0) {
        char val = '1';
        fs_read(&file, &val, 1);
        fs_close(&file);
        s_fake_internet = (val != '0');
    } else {
        s_fake_internet = true;
    }
    captive_dns_set_enabled(s_fake_internet);
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

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
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
            LOG_INF("AP Station joined: " MACSTR " (active clients: %u)",
                    info->mac[0], info->mac[1], info->mac[2],
                    info->mac[3], info->mac[4], info->mac[5], s_sta_clients);
        } else {
            LOG_INF("AP Station joined (active clients: %u)", s_sta_clients);
        }
        break;
    }

    case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
        struct wifi_ap_sta_info *info = (struct wifi_ap_sta_info *)cb->info;
        if (s_sta_clients > 0) {
            s_sta_clients--;
        }
        if (info) {
            LOG_INF("AP Station left: " MACSTR " (active clients: %u)",
                    info->mac[0], info->mac[1], info->mac[2],
                    info->mac[3], info->mac[4], info->mac[5], s_sta_clients);
        } else {
            LOG_INF("AP Station left (active clients: %u)", s_sta_clients);
        }
        break;
    }

    case NET_EVENT_WIFI_CONNECT_RESULT: {
        const struct wifi_status *status = (const struct wifi_status *)cb->info;
        if (status && status->status == 0) {
            LOG_INF("Wi-Fi Station connected to '%s'. Awaiting DHCPv4 lease...", s_sta_ssid);
            s_sta_connected = true;
        } else {
            LOG_WRN("Wi-Fi Station connection attempt failed (%d)", status ? status->status : -1);
            s_sta_connected = false;
        }
        break;
    }

    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_INF("Wi-Fi Station disconnected from '%s'.", s_sta_ssid);
        s_sta_connected = false;
        strcpy(s_sta_ip, "0.0.0.0");
        break;

    case NET_EVENT_IPV4_ADDR_ADD:
    case NET_EVENT_IPV4_DHCP_BOUND: {
        if (iface == s_sta_iface && iface->config.ip.ipv4) {
            for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
                if (iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
                    char buf[NET_IPV4_ADDR_LEN];
                    net_addr_ntop(AF_INET,
                                  &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
                                  buf, sizeof(buf));
                    strncpy(s_sta_ip, buf, sizeof(s_sta_ip) - 1);
                    s_sta_connected = true;
                    LOG_INF("Station DHCP bound! IP Address: %s (mDNS: %s)",
                            s_sta_ip, s_mdns_domain);
                    break;
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

static int start_dhcpv4_server(void)
{
    if (!s_ap_iface) {
        return -ENODEV;
    }

    struct in_addr addr;
    struct in_addr netmask;

    if (net_addr_pton(AF_INET, s_ap_ip, &addr) < 0) {
        LOG_ERR("Invalid AP IP address: %s", s_ap_ip);
        return -EINVAL;
    }

    if (net_addr_pton(AF_INET, ESPIRATE_DEFAULT_MASK, &netmask) < 0) {
        LOG_ERR("Invalid AP netmask: %s", ESPIRATE_DEFAULT_MASK);
        return -EINVAL;
    }

    net_if_ipv4_set_gw(s_ap_iface, &addr);

    if (net_if_ipv4_addr_add(s_ap_iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
        LOG_WRN("AP IPv4 address already assigned or failed to add");
    }

    if (!net_if_ipv4_set_netmask_by_addr(s_ap_iface, &addr, &netmask)) {
        LOG_ERR("Failed to set netmask for AP interface");
        return -EINVAL;
    }

    struct in_addr pool_start = addr;
    pool_start.s4_addr[3] = 10;

    int ret = net_dhcpv4_server_start(s_ap_iface, &pool_start);
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("DHCPv4 server failed to start: %d", ret);
        return ret;
    }

    s_dhcp_srv_active = true;
    LOG_INF("DHCPv4 server active at %s (Router/DNS: %s, Pool: .10 - .50)",
            s_ap_ip, s_ap_ip);
    return 0;
}

int wifi_manager_init(void)
{
    /* Read hardware MAC and configure dynamic hostname espirateXXYY */
    esp_read_mac(s_mac, ESP_MAC_WIFI_STA);
    snprintf(s_hostname, sizeof(s_hostname), "espirate%02x%02x", s_mac[4], s_mac[5]);
    snprintf(s_mdns_domain, sizeof(s_mdns_domain), "%s.local", s_hostname);

    net_hostname_set(s_hostname, strlen(s_hostname));
    LOG_INF("Hardware MAC: " MACSTR " | Dynamic Hostname: %s (mDNS: %s)",
            s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5],
            s_hostname, s_mdns_domain);

    net_mgmt_init_event_callback(&s_mgmt_cb, wifi_mgmt_event_handler, NET_EVENT_WIFI_ALL_MASK);
    net_mgmt_add_event_callback(&s_mgmt_cb);

    s_ap_iface = net_if_get_wifi_sap();
    s_sta_iface = net_if_get_wifi_sta();

    if (!s_ap_iface) {
        LOG_ERR("No Wi-Fi Soft-AP interface found");
        return -ENODEV;
    }

    /* Start default Soft-AP */
    wifi_manager_start_ap(ESPIRATE_DEFAULT_SSID, NULL);

    /* Initialize Fake Internet flag and captive DNS */
    load_fake_internet_flag();

    /* Check if saved STA credentials exist and initiate background connection */
    char saved_ssid[33] = {0};
    char saved_pass[65] = {0};
    if (load_credentials_safely(saved_ssid, sizeof(saved_ssid),
                                saved_pass, sizeof(saved_pass)) == 0 &&
        strlen(saved_ssid) > 0) {
        LOG_INF("Found saved Wi-Fi station config for '%s'. Connecting...", saved_ssid);
        wifi_manager_connect_sta(saved_ssid, saved_pass, false);
    } else {
        LOG_INF("No saved Wi-Fi station configuration.");
    }

    return 0;
}

int wifi_manager_start_ap(const char *ssid, const char *password)
{
    if (!s_ap_iface) {
        s_ap_iface = net_if_get_wifi_sap();
        if (!s_ap_iface) {
            LOG_ERR("Wi-Fi AP interface unavailable");
            return -ENODEV;
        }
    }

    if (s_ap_active) {
        wifi_manager_stop_ap();
        k_msleep(200);
    }

    const char *ap_ssid = (ssid && strlen(ssid) > 0) ? ssid : ESPIRATE_DEFAULT_SSID;
    strncpy(s_ap_ssid, ap_ssid, sizeof(s_ap_ssid) - 1);
    s_ap_ssid[sizeof(s_ap_ssid) - 1] = '\0';

    memset(&s_ap_config, 0, sizeof(s_ap_config));
    s_ap_config.ssid = (const uint8_t *)s_ap_ssid;
    s_ap_config.ssid_length = strlen(s_ap_ssid);
    s_ap_config.channel = 1;
    s_ap_config.band = WIFI_FREQ_BAND_2_4_GHZ;

    if (password && strlen(password) >= 8) {
        s_ap_config.psk = (const uint8_t *)password;
        s_ap_config.psk_length = strlen(password);
        s_ap_config.security = WIFI_SECURITY_TYPE_PSK;
    } else {
        s_ap_config.security = WIFI_SECURITY_TYPE_NONE;
    }

    LOG_INF("Enabling Wi-Fi Soft-AP '%s' (Security: %s, Channel: %d)...",
            s_ap_ssid,
            (s_ap_config.security == WIFI_SECURITY_TYPE_PSK) ? "WPA2-PSK" : "Open",
            s_ap_config.channel);

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, s_ap_iface, &s_ap_config, sizeof(s_ap_config));
    if (ret != 0) {
        LOG_ERR("NET_REQUEST_WIFI_AP_ENABLE failed: %d", ret);
        return ret;
    }

    s_ap_active = true;
    return 0;
}

int wifi_manager_stop_ap(void)
{
    if (!s_ap_iface || !s_ap_active) {
        return 0;
    }

    LOG_INF("Disabling Wi-Fi Soft-AP '%s'...", s_ap_ssid);
    if (s_dhcp_srv_active) {
        net_dhcpv4_server_stop(s_ap_iface);
        s_dhcp_srv_active = false;
    }

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, s_ap_iface, NULL, 0);
    s_ap_active = false;
    s_sta_clients = 0;
    return ret;
}

bool wifi_manager_is_ap_active(void)
{
    return s_ap_active;
}

const char *wifi_manager_get_ssid(void)
{
    return s_ap_ssid;
}

const char *wifi_manager_get_ip(void)
{
    return s_ap_ip;
}

uint32_t wifi_manager_get_station_count(void)
{
    return s_sta_clients;
}

int wifi_manager_connect_sta(const char *ssid, const char *password, bool save)
{
    if (!s_sta_iface) {
        s_sta_iface = net_if_get_wifi_sta();
        if (!s_sta_iface) {
            LOG_ERR("Station interface not available");
            return -ENODEV;
        }
    }

    if (!ssid || strlen(ssid) == 0) {
        return -EINVAL;
    }

    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';

    static char static_pass[65];
    strncpy(static_pass, password ? password : "", sizeof(static_pass) - 1);
    static_pass[sizeof(static_pass) - 1] = '\0';

    memset(&s_sta_config, 0, sizeof(s_sta_config));
    s_sta_config.ssid = (const uint8_t *)s_sta_ssid;
    s_sta_config.ssid_length = strlen(s_sta_ssid);
    s_sta_config.channel = WIFI_CHANNEL_ANY;
    s_sta_config.band = WIFI_FREQ_BAND_2_4_GHZ;
    s_sta_config.mfp = WIFI_MFP_OPTIONAL;

    if (strlen(static_pass) > 0) {
        s_sta_config.psk = (const uint8_t *)static_pass;
        s_sta_config.psk_length = strlen(static_pass);
        s_sta_config.security = WIFI_SECURITY_TYPE_PSK;
    } else {
        s_sta_config.security = WIFI_SECURITY_TYPE_NONE;
    }

    if (save) {
        save_credentials_safely(ssid, password);
    }

    LOG_INF("Connecting Wi-Fi Station to '%s'...", s_sta_ssid);
    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, s_sta_iface, &s_sta_config, sizeof(s_sta_config));
    if (ret != 0) {
        LOG_ERR("NET_REQUEST_WIFI_CONNECT failed: %d", ret);
        return ret;
    }

    return 0;
}

int wifi_manager_disconnect_sta(void)
{
    if (!s_sta_iface) {
        return 0;
    }

    LOG_INF("Disconnecting Wi-Fi Station...");
    int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, s_sta_iface, NULL, 0);
    s_sta_connected = false;
    strcpy(s_sta_ip, "0.0.0.0");
    return ret;
}

int wifi_manager_forget_sta(void)
{
    wifi_manager_disconnect_sta();
    s_sta_ssid[0] = '\0';
    int ret = fs_unlink(WIFI_STA_FILE);
    LOG_INF("Saved station credentials removed from flash (%d)", ret);
    return ret;
}

bool wifi_manager_sta_is_connected(void)
{
    return s_sta_connected;
}

bool wifi_manager_has_saved_sta(void)
{
    struct fs_dirent entry;
    return (fs_stat(WIFI_STA_FILE, &entry) == 0);
}

const char *wifi_manager_get_sta_ssid(void)
{
    if (strlen(s_sta_ssid) > 0) {
        return s_sta_ssid;
    }
    static char saved[33];
    if (load_credentials_safely(saved, sizeof(saved), NULL, 0) == 0) {
        return saved;
    }
    return "";
}

const char *wifi_manager_get_sta_ip(void)
{
    return s_sta_ip;
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
    captive_dns_set_enabled(enable);
    LOG_INF("Pretend Internet Connectivity set to: %s", enable ? "ENABLED" : "DISABLED");
}

bool wifi_manager_get_fake_internet(void)
{
    return s_fake_internet;
}
