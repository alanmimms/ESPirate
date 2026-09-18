/*
 * Copyright (c) 2026 Alan Mimms / ESPirate
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <string.h>
#include <stdint.h>
#include "captive_dns.h"

LOG_MODULE_REGISTER(captive_dns, LOG_LEVEL_INF);

#define DNS_PORT 53
#define DNS_BUF_SIZE 512

K_THREAD_STACK_DEFINE(dns_thread_stack, 3072);
static struct k_thread dns_thread_data;
static bool s_enabled = true;
static int s_dns_sock = -1;

void captive_dns_set_enabled(bool enable)
{
    s_enabled = enable;
    LOG_INF("Captive DNS %s", enable ? "ENABLED (Pretend Internet ON)" : "DISABLED");
}

bool captive_dns_is_enabled(void)
{
    return s_enabled;
}

static void dns_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Allow network interfaces to initialize */
    k_sleep(K_MSEC(1200));

    s_dns_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock < 0) {
        LOG_ERR("Failed to create DNS UDP socket: %d", errno);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (zsock_bind(s_dns_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Failed to bind DNS socket to port %d: %d", DNS_PORT, errno);
        zsock_close(s_dns_sock);
        s_dns_sock = -1;
        return;
    }

    LOG_INF("Captive DNS responder active on UDP port %d", DNS_PORT);

    static uint8_t req_buf[DNS_BUF_SIZE];
    static uint8_t resp_buf[DNS_BUF_SIZE];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        ssize_t len = zsock_recvfrom(s_dns_sock, req_buf, sizeof(req_buf), 0,
                                     (struct sockaddr *)&client_addr, &client_len);
        if (len < 12) {
            continue;
        }

        if (!s_enabled) {
            continue;
        }

        /* Verify standard query (QR == 0) */
        if ((req_buf[2] & 0x80) != 0) {
            continue; /* Not a query */
        }

        /* Locate question section end */
        size_t pos = 12;
        while (pos < (size_t)len && req_buf[pos] != 0) {
            pos += 1 + req_buf[pos];
        }
        pos += 1; /* Skip trailing null byte of QNAME */
        pos += 4; /* Skip QTYPE (2 bytes) and QCLASS (2 bytes) */

        if (pos > (size_t)len || pos + 16 > sizeof(resp_buf)) {
            continue;
        }

        /* Build Response Header */
        memcpy(resp_buf, req_buf, pos);
        resp_buf[2] = 0x81; /* QR=1 (response), AA=1, RD=1 */
        resp_buf[3] = 0x80; /* RA=1, RCODE=0 (no error) */
        resp_buf[4] = 0x00; resp_buf[5] = 0x01; /* QDCOUNT = 1 */
        resp_buf[6] = 0x00; resp_buf[7] = 0x01; /* ANCOUNT = 1 */
        resp_buf[8] = 0x00; resp_buf[9] = 0x00; /* NSCOUNT = 0 */
        resp_buf[10] = 0x00; resp_buf[11] = 0x00; /* ARCOUNT = 0 */

        /* Append Answer Section (pointing to QNAME at offset 12) */
        resp_buf[pos++] = 0xc0; /* Pointer label */
        resp_buf[pos++] = 0x0c; /* Offset 12 */
        resp_buf[pos++] = 0x00; resp_buf[pos++] = 0x01; /* Type A */
        resp_buf[pos++] = 0x00; resp_buf[pos++] = 0x01; /* Class IN */
        resp_buf[pos++] = 0x00; resp_buf[pos++] = 0x00;
        resp_buf[pos++] = 0x00; resp_buf[pos++] = 0x3c; /* TTL: 60s */
        resp_buf[pos++] = 0x00; resp_buf[pos++] = 0x04; /* Length: 4 bytes */

        /* Target IPv4 address: 192.168.4.1 */
        resp_buf[pos++] = 192;
        resp_buf[pos++] = 168;
        resp_buf[pos++] = 4;
        resp_buf[pos++] = 1;

        zsock_sendto(s_dns_sock, resp_buf, pos, 0,
                     (struct sockaddr *)&client_addr, client_len);
    }
}

int captive_dns_init(void)
{
    k_thread_create(&dns_thread_data,
                    dns_thread_stack,
                    K_THREAD_STACK_SIZEOF(dns_thread_stack),
                    dns_thread_fn,
                    NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);
    k_thread_name_set(&dns_thread_data, "captive_dns");
    return 0;
}
