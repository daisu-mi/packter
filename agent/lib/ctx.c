#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "packter.h"

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
