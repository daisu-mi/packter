/*
 * GeoIP lookup for PACKTEARTH mode. The legacy libGeoIP is deprecated;
 * build with -DPACKTER_HAVE_GEOIP and -lGeoIP to enable the real lookup,
 * otherwise -G reports an explanatory error. Broker-side enrichment with
 * MaxMind GeoIP2 replaces this in phase 5.
 */
#include <stdio.h>
#include <string.h>

#include "packter.h"

#ifdef PACKTER_HAVE_GEOIP
#include <GeoIP.h>
#include <GeoIPCity.h>

int packter_geoip_lookup(packter_ctx *ctx, const char *host, char *out, size_t outlen)
{
    static GeoIP *gi = NULL;
    GeoIPRecord *gir;

    if (gi == NULL) {
        gi = GeoIP_open(ctx->geoip_datfile, GEOIP_INDEX_CACHE);
        if (gi == NULL) {
            fprintf(stderr, "GeoIP_open failed: %s\n", ctx->geoip_datfile);
            return PACKTER_FALSE;
        }
    }
    gir = GeoIP_record_by_name(gi, host);
    if (gir == NULL) {
        return PACKTER_FALSE;
    }
    snprintf(out, outlen, "%f,%f", gir->latitude, gir->longitude);
    GeoIPRecord_delete(gir);
    return PACKTER_TRUE;
}
#else
int packter_geoip_lookup(packter_ctx *ctx, const char *host, char *out, size_t outlen)
{
    (void)host;
    (void)out;
    (void)outlen;
    if (ctx->debug == PACKTER_TRUE) {
        fprintf(stderr, "GeoIP support is not built in (rebuild with GEOIP=1)\n");
    }
    return PACKTER_FALSE;
}
#endif
