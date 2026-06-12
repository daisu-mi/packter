/*
 * IP -> latitude/longitude lookup for PACKTEARTH mode.
 *
 * Uses libmaxminddb (the MMDB reader) against a DB-IP "IP to City Lite"
 * database (CC BY 4.0 — redistributable with attribution), which replaces the
 * old, non-redistributable MaxMind GeoLite/GeoLiteCity + legacy libGeoIP path.
 * The schema (location/latitude, location/longitude) is the GeoLite2-City
 * layout, so a GeoLite2 City MMDB also works if you hold a licence for it.
 *
 * Enabled by configure when libmaxminddb is found (--with-geoip), which
 * #defines PACKTER_HAVE_GEOIP in config.h; otherwise -G reports an
 * explanatory error and the rest of the agent is unaffected.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "packter.h"

#ifdef PACKTER_HAVE_GEOIP
#include <maxminddb.h>

int packter_geoip_lookup(packter_ctx *ctx, const char *host, char *out, size_t outlen)
{
    static MMDB_s mmdb;
    static int opened = 0;
    int gai_error = 0, mmdb_error = 0;
    MMDB_lookup_result_s res;
    MMDB_entry_data_s lat, lon;

    if (!opened) {
        if (MMDB_open(ctx->geoip_datfile, MMDB_MODE_MMAP, &mmdb) != MMDB_SUCCESS) {
            fprintf(stderr, "MMDB_open failed: %s\n", ctx->geoip_datfile);
            return PACKTER_FALSE;
        }
        opened = 1;   /* held open for the process lifetime */
    }

    res = MMDB_lookup_string(&mmdb, host, &gai_error, &mmdb_error);
    if (gai_error != 0 || mmdb_error != MMDB_SUCCESS || !res.found_entry) {
        return PACKTER_FALSE;
    }
    if (MMDB_get_value(&res.entry, &lat, "location", "latitude", NULL) != MMDB_SUCCESS ||
        MMDB_get_value(&res.entry, &lon, "location", "longitude", NULL) != MMDB_SUCCESS ||
        !lat.has_data || !lon.has_data ||
        lat.type != MMDB_DATA_TYPE_DOUBLE || lon.type != MMDB_DATA_TYPE_DOUBLE) {
        return PACKTER_FALSE;
    }
    snprintf(out, outlen, "%f,%f", lat.double_value, lon.double_value);
    return PACKTER_TRUE;
}
#else
int packter_geoip_lookup(packter_ctx *ctx, const char *host, char *out, size_t outlen)
{
    (void)host;
    (void)out;
    (void)outlen;
    if (ctx->debug == PACKTER_TRUE) {
        fprintf(stderr, "GeoIP support is not built in (rebuild with GEOIP=1, needs libmaxminddb)\n");
    }
    return PACKTER_FALSE;
}
#endif
