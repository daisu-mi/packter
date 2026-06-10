/*
 * Send path. Wire-compatible with 2.5:
 *   - each message is "HEADER\n<payload>\n" sent as one UDP datagram
 *   - -n prints "TIME <sec> <usec>" + the message instead (pt_replay format)
 *   - -R drops messages by the 2.5 randomized countdown
 *
 * New in 3.0: -B <ms> bulk mode. PACKTER fly records accumulate (header
 * stripped) and flush as a single "PACKTER\n<rec>\n<rec>\n..." datagram
 * when the window expires or the next record would exceed an MTU-safe
 * payload. Non-fly messages (PACKTERSE etc.) flush the buffer first so
 * ordering is preserved. The bulk form is accepted by Viewer 2.4 as-is.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "packter.h"

static void emit(packter_ctx *ctx, const char *mesg, size_t len)
{
    struct timeval now;

    if (ctx->notsend == PACKTER_TRUE) {
        if (gettimeofday(&now, NULL) < 0) {
            perror("gettimeofday");
            exit(EXIT_FAILURE);
        }
        printf("TIME %ld %ld\n", (long)now.tv_sec, (long)now.tv_usec);
        fwrite(mesg, 1, len, stdout);
        return;
    }
    if (ctx->use6 == PACKTER_TRUE) {
        if (sendto(ctx->sock, mesg, len, 0,
                   (struct sockaddr *)&ctx->addr6, sizeof(ctx->addr6)) < 0) {
            perror("sendto");
        }
    } else {
        if (sendto(ctx->sock, mesg, len, 0,
                   (struct sockaddr *)&ctx->addr, sizeof(ctx->addr)) < 0) {
            perror("sendto");
        }
    }
}

void packter_flush(packter_ctx *ctx)
{
    char frame[sizeof(PACKTER_HEADER) + PACKTER_BULK_PAYLOAD];
    size_t len;

    if (ctx->bulk_len == 0) {
        return;
    }
    len = (size_t)snprintf(frame, sizeof(frame), "%s", PACKTER_HEADER);
    memcpy(frame + len, ctx->bulk_buf, ctx->bulk_len);
    len += ctx->bulk_len;
    emit(ctx, frame, len);
    ctx->bulk_len = 0;
}

static int bulk_window_expired(packter_ctx *ctx)
{
    struct timeval now;
    unsigned long ms;

    if (ctx->bulk_len == 0) {
        return 0;
    }
    gettimeofday(&now, NULL);
    ms = packter_diff_sec(&now, &ctx->bulk_first) * 1000
       + packter_diff_usec(&now, &ctx->bulk_first) / 1000;
    return ms >= (unsigned long)ctx->bulk_ms;
}

void packter_send(packter_ctx *ctx, const char *mesg)
{
    size_t len;

    /* 2.5 randomized rate limiting, unchanged */
    if (ctx->rate != 1) {
        ctx->rate -= 1;
        return;
    }
    ctx->rate = packter_rate(ctx->rate_limit);

    if (ctx->debug == PACKTER_TRUE) {
        printf("%s", mesg);
    }

    len = strlen(mesg);

    if (ctx->bulk_ms > 0 &&
        strncmp(mesg, PACKTER_HEADER, sizeof(PACKTER_HEADER) - 1) == 0) {
        const char *rec = mesg + sizeof(PACKTER_HEADER) - 1;
        size_t reclen = len - (sizeof(PACKTER_HEADER) - 1);

        if (reclen > PACKTER_BULK_PAYLOAD) {
            packter_flush(ctx);
            emit(ctx, mesg, len);
            return;
        }
        if (ctx->bulk_len + reclen > PACKTER_BULK_PAYLOAD) {
            packter_flush(ctx);
        }
        if (ctx->bulk_len == 0) {
            gettimeofday(&ctx->bulk_first, NULL);
        }
        memcpy(ctx->bulk_buf + ctx->bulk_len, rec, reclen);
        ctx->bulk_len += reclen;
        if (bulk_window_expired(ctx)) {
            packter_flush(ctx);
        }
        return;
    }

    /* non-fly message (or bulk off): keep ordering, then send directly */
    packter_flush(ctx);
    emit(ctx, mesg, len);
}

void packter_send_se(packter_ctx *ctx, int flag)
{
    char se[PACKTER_BUFSIZ];
    snprintf(se, PACKTER_BUFSIZ, "%sse%d.wav", PACKTER_SE, flag);
    packter_send(ctx, se);
}
