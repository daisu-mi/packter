/*
 * PACKTER record formatting. Format strings are byte-identical to 2.5
 * (pt_mesg.c): PACKTER for IP endpoints, PACKTEARTH for GeoIP lat/long.
 */
#include <stdio.h>
#include <string.h>

#include "packter.h"

void packter_mesg(packter_ctx *ctx, char *mesg, size_t mesglen,
                  const char *srcip, const char *dstip,
                  int data1, int data2, int flag, const char *desc)
{
    if (ctx->geoip == PACKTER_TRUE) {
        char srcgeo[PACKTER_BUFSIZ];
        char dstgeo[PACKTER_BUFSIZ];

        if (packter_geoip_lookup(ctx, srcip, srcgeo, sizeof(srcgeo)) == PACKTER_FALSE ||
            packter_geoip_lookup(ctx, dstip, dstgeo, sizeof(dstgeo)) == PACKTER_FALSE) {
            mesg[0] = '\0';
            return;
        }
        if (ctx->trace == PACKTER_TRUE) {
            snprintf(mesg, mesglen, "%s%s,%s,%d,%s-%s\n",
                     PACKTER_EARTH, srcgeo, dstgeo,
                     flag + ctx->flagbase, desc, ctx->trace_server);
        } else {
            snprintf(mesg, mesglen, "%s%s,%s,%d,%s\n",
                     PACKTER_EARTH, srcgeo, dstgeo,
                     flag + ctx->flagbase, desc);
        }
        return;
    }

    if (ctx->trace == PACKTER_TRUE) {
        snprintf(mesg, mesglen, "%s%s,%s,%d,%d,%d,%s-%s\n",
                 PACKTER_HEADER, srcip, dstip, data1, data2,
                 flag + ctx->flagbase, desc, ctx->trace_server);
    } else {
        snprintf(mesg, mesglen, "%s%s,%s,%d,%d,%d,%s\n",
                 PACKTER_HEADER, srcip, dstip, data1, data2,
                 flag + ctx->flagbase, desc);
    }
}
