/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4_server.h>
#include <string.h>

#include "wifi_manager.h"

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define NET_EVENT_WIFI_AP_MASK \
    (NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT | \
     NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

static struct net_if *ap_iface = NULL;
static struct wifi_connect_req_params ap_config;
static struct net_mgmt_event_callback wifi_mgmt_cb;

static bool s_is_active = false;
static bool s_dhcp_active = false;
static char s_current_ssid[33] = ESPIRATE_DEFAULT_SSID;
static char s_current_ip[16] = ESPIRATE_DEFAULT_IP;
static uint32_t s_sta_count = 0;

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint64_t mgmt_event,
                                   struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_AP_ENABLE_RESULT:
        LOG_INF("Wi-Fi Soft-AP '%s' enabled and broadcasting.", s_current_ssid);
        s_is_active = true;
        break;

    case NET_EVENT_WIFI_AP_DISABLE_RESULT:
        LOG_INF("Wi-Fi Soft-AP disabled.");
        s_is_active = false;
        s_sta_count = 0;
        break;

    case NET_EVENT_WIFI_AP_STA_CONNECTED: {
        struct wifi_ap_sta_info *info = (struct wifi_ap_sta_info *)cb->info;
        s_sta_count++;
        if (info) {
            LOG_INF("Station connected: " MACSTR " (active clients: %u)",
                    info->mac[0], info->mac[1], info->mac[2],
                    info->mac[3], info->mac[4], info->mac[5], s_sta_count);
        } else {
            LOG_INF("Station connected (active clients: %u)", s_sta_count);
        }
        break;
    }

    case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
        struct wifi_ap_sta_info *info = (struct wifi_ap_sta_info *)cb->info;
        if (s_sta_count > 0) {
            s_sta_count--;
        }
        if (info) {
            LOG_INF("Station disconnected: " MACSTR " (active clients: %u)",
                    info->mac[0], info->mac[1], info->mac[2],
                    info->mac[3], info->mac[4], info->mac[5], s_sta_count);
        } else {
            LOG_INF("Station disconnected (active clients: %u)", s_sta_count);
        }
        break;
    }

    default:
        break;
    }
}

static int start_dhcpv4_server(void)
{
    if (!ap_iface) {
        return -ENODEV;
    }

    struct in_addr addr;
    struct in_addr netmask;

    if (net_addr_pton(AF_INET, s_current_ip, &addr) < 0) {
        LOG_ERR("Invalid AP IP address: %s", s_current_ip);
        return -EINVAL;
    }

    if (net_addr_pton(AF_INET, ESPIRATE_DEFAULT_MASK, &netmask) < 0) {
        LOG_ERR("Invalid AP netmask: %s", ESPIRATE_DEFAULT_MASK);
        return -EINVAL;
    }

    net_if_ipv4_set_gw(ap_iface, &addr);

    if (net_if_ipv4_addr_add(ap_iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
        LOG_WRN("AP IPv4 address already assigned or failed to add");
    }

    if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &addr, &netmask)) {
        LOG_ERR("Failed to set netmask for AP interface");
        return -EINVAL;
    }

    /* Pool starts at .10 (192.168.4.10) */
    struct in_addr pool_start = addr;
    pool_start.s4_addr[3] = 10;

    int ret = net_dhcpv4_server_start(ap_iface, &pool_start);
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("DHCPv4 server failed to start: %d", ret);
        return ret;
    }

    s_dhcp_active = true;
    LOG_INF("DHCPv4 server active at %s (pool: 192.168.4.10 - 192.168.4.50)", s_current_ip);
    return 0;
}

int wifi_manager_init(void)
{
    net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler, NET_EVENT_WIFI_AP_MASK);
    net_mgmt_add_event_callback(&wifi_mgmt_cb);

    ap_iface = net_if_get_wifi_sap();
    if (!ap_iface) {
        LOG_ERR("No Wi-Fi SAP interface found");
        return -ENODEV;
    }

    LOG_INF("Wi-Fi manager initialized. Starting default Soft-AP...");
    return wifi_manager_start_ap(ESPIRATE_DEFAULT_SSID, NULL);
}

int wifi_manager_start_ap(const char *ssid, const char *password)
{
    if (!ap_iface) {
        ap_iface = net_if_get_wifi_sap();
        if (!ap_iface) {
            LOG_ERR("Wi-Fi interface unavailable");
            return -ENODEV;
        }
    }

    if (s_is_active) {
        wifi_manager_stop_ap();
        k_msleep(200);
    }

    const char *ap_ssid = (ssid && strlen(ssid) > 0) ? ssid : ESPIRATE_DEFAULT_SSID;
    strncpy(s_current_ssid, ap_ssid, sizeof(s_current_ssid) - 1);
    s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';

    memset(&ap_config, 0, sizeof(ap_config));
    ap_config.ssid = (const uint8_t *)s_current_ssid;
    ap_config.ssid_length = strlen(s_current_ssid);
    ap_config.channel = 1;
    ap_config.band = WIFI_FREQ_BAND_2_4_GHZ;

    if (password && strlen(password) >= 8) {
        ap_config.psk = (const uint8_t *)password;
        ap_config.psk_length = strlen(password);
        ap_config.security = WIFI_SECURITY_TYPE_PSK;
    } else {
        ap_config.security = WIFI_SECURITY_TYPE_NONE;
    }

    /* Start DHCP server first so it is bound before clients connect */
    start_dhcpv4_server();

    LOG_INF("Enabling Wi-Fi Soft-AP '%s' (Security: %s, Channel: %d)...",
            s_current_ssid,
            (ap_config.security == WIFI_SECURITY_TYPE_PSK) ? "WPA2-PSK" : "Open",
            ap_config.channel);

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &ap_config, sizeof(ap_config));
    if (ret != 0) {
        LOG_ERR("NET_REQUEST_WIFI_AP_ENABLE failed: %d", ret);
        return ret;
    }

    s_is_active = true;
    return 0;
}

int wifi_manager_stop_ap(void)
{
    if (!ap_iface || !s_is_active) {
        return 0;
    }

    LOG_INF("Disabling Wi-Fi Soft-AP '%s'...", s_current_ssid);
    if (s_dhcp_active) {
        net_dhcpv4_server_stop(ap_iface);
        s_dhcp_active = false;
    }

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap_iface, NULL, 0);
    s_is_active = false;
    s_sta_count = 0;
    return ret;
}

bool wifi_manager_is_ap_active(void)
{
    return s_is_active;
}

const char *wifi_manager_get_ssid(void)
{
    return s_current_ssid;
}

const char *wifi_manager_get_ip(void)
{
    return s_current_ip;
}

uint32_t wifi_manager_get_station_count(void)
{
    return s_sta_count;
}
