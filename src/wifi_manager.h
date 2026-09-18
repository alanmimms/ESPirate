/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_WIFI_MANAGER_H_
#define ESPIRATE_WIFI_MANAGER_H_

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

#define ESPIRATE_DEFAULT_SSID "ESPirate-AP"
#define ESPIRATE_DEFAULT_IP   "192.168.4.1"
#define ESPIRATE_DEFAULT_MASK "255.255.255.0"

#ifdef __cplusplus
extern "C" {
#endif

/* Subsystem Initialization */
int wifi_manager_init(void);

/* Soft-AP Management */
int wifi_manager_start_ap(const char *ssid, const char *password);
int wifi_manager_stop_ap(void);
bool wifi_manager_is_ap_active(void);
const char *wifi_manager_get_ssid(void);
const char *wifi_manager_get_ip(void);
uint32_t wifi_manager_get_station_count(void);

/* Station (STA) Client Management */
int wifi_manager_connect_sta(const char *ssid, const char *password, bool save);
int wifi_manager_disconnect_sta(void);
int wifi_manager_forget_sta(void);
bool wifi_manager_sta_is_connected(void);
bool wifi_manager_has_saved_sta(void);
const char *wifi_manager_get_sta_ssid(void);
const char *wifi_manager_get_sta_ip(void);

/* Hostname & mDNS Identification */
const char *wifi_manager_get_hostname(void);
const char *wifi_manager_get_mdns_domain(void);

/* Pretend Internet Connectivity (Android 204 & Captive DNS) */
void wifi_manager_set_fake_internet(bool enable);
bool wifi_manager_get_fake_internet(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_WIFI_MANAGER_H_ */
