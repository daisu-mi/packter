#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "packter.h"

/*
 * Open a dual-stack UDP listening socket. An AF_INET6 socket with
 * IPV6_V6ONLY disabled receives both IPv6 and (v4-mapped) IPv4, so a single
 * collector serves exporters on either family. bind_addr may be a v6 literal,
 * a v4 literal (mapped to ::ffff:a.b.c.d), or NULL for in6addr_any.
 */
int packter_udp_listen(const char *bind_addr, int port)
{
    int sock, off = 0;
    struct sockaddr_in6 sa;

    if ((sock = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off)) < 0) {
        /* non-fatal: the socket still works, just IPv6-only */
        perror("setsockopt(IPV6_V6ONLY)");
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons((uint16_t)port);
    sa.sin6_addr = in6addr_any;

    if (bind_addr != NULL) {
        struct in_addr v4;
        if (inet_pton(AF_INET6, bind_addr, &sa.sin6_addr) == 1) {
            /* IPv6 literal */
        } else if (inet_pton(AF_INET, bind_addr, &v4) == 1) {
            /* IPv4 literal -> v4-mapped ::ffff:a.b.c.d */
            memset(&sa.sin6_addr, 0, sizeof(sa.sin6_addr));
            sa.sin6_addr.s6_addr[10] = 0xff;
            sa.sin6_addr.s6_addr[11] = 0xff;
            memcpy(&sa.sin6_addr.s6_addr[12], &v4, 4);
        } else {
            fprintf(stderr, "invalid bind address: %s\n", bind_addr);
            exit(EXIT_FAILURE);
        }
    }

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    return sock;
}

void packter_ctx_init(packter_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->debug = PACKTER_FALSE;
    ctx->notsend = PACKTER_FALSE;
    ctx->use6 = PACKTER_FALSE;
    ctx->trace = PACKTER_FALSE;
    ctx->snort_report = PACKTER_FALSE;
    ctx->enable_sound = PACKTER_FALSE;
    ctx->geoip = PACKTER_FALSE;
    ctx->flagbase = 0;
    ctx->rate_limit = 1;
    ctx->sock = -1;
    ctx->addr.sin_family = AF_INET;
    ctx->addr6.sin6_family = AF_INET6;
    ctx->bulk_ms = 0;
    ctx->bulk_len = 0;
    srand((unsigned)time(NULL));
    ctx->rate = packter_rate(ctx->rate_limit);
}

/*
 * Resolve the viewer address and open the UDP socket. The address family
 * is detected at runtime from the address text — 2.5 needed a compile-time
 * --enable-ipv6 plus an explicit -6 flag for this.
 */
int packter_connect(packter_ctx *ctx, const char *ip, int port)
{
    if (port <= 0) {
        port = PACKTER_VIEWER_PORT;
    }
    if (inet_pton(AF_INET, ip, &ctx->addr.sin_addr) == 1) {
        ctx->use6 = PACKTER_FALSE;
        ctx->addr.sin_port = htons((uint16_t)port);
        ctx->sock = socket(PF_INET, SOCK_DGRAM, 0);
    } else if (inet_pton(AF_INET6, ip, &ctx->addr6.sin6_addr) == 1) {
        ctx->use6 = PACKTER_TRUE;
        ctx->addr6.sin6_port = htons((uint16_t)port);
        ctx->sock = socket(PF_INET6, SOCK_DGRAM, 0);
    } else {
        fprintf(stderr, "invalid viewer address: %s\n", ip);
        return -1;
    }
    if (ctx->sock < 0) {
        perror("socket");
        return -1;
    }
    return ctx->sock;
}
