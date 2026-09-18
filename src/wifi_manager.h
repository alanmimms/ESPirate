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

int wifi_manager_init(void);
int wifi_manager_start_ap(const char *ssid, const char *password);
int wifi_manager_stop_ap(void);
bool wifi_manager_is_ap_active(void);
const char *wifi_manager_get_ssid(void);
const char *wifi_manager_get_ip(void);
uint32_t wifi_manager_get_station_count(void);

#endif /* ESPIRATE_WIFI_MANAGER_H_ */
