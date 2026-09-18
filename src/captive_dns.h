/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPIRATE_CAPTIVE_DNS_H_
#define ESPIRATE_CAPTIVE_DNS_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the captive DNS responder thread on UDP port 53.
 * @return 0 on success, negative error code on failure.
 */
int captive_dns_init(void);

/**
 * @brief Enable or disable captive DNS resolution (pretend internet).
 * @param enable true to resolve all DNS queries to 192.168.4.1, false to drop queries.
 */
void captive_dns_set_enabled(bool enable);

/**
 * @brief Check if captive DNS is currently enabled.
 * @return true if enabled, false otherwise.
 */
bool captive_dns_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPIRATE_CAPTIVE_DNS_H_ */
