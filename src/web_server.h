/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_WEB_SERVER_H_
#define ESPIRATE_WEB_SERVER_H_

#include <stdbool.h>

int web_server_init(void);
bool web_server_is_running(void);
void web_server_notify_network_change(void);

#endif /* ESPIRATE_WEB_SERVER_H_ */
