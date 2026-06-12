#!/usr/bin/env bash
#
# Fetch the DB-IP "IP to City Lite" MMDB for PACKTEARTH (pt_agent -G).
#
# WHY THIS IS A MANUAL, OPT-IN STEP
#   The data is licensed CC BY 4.0 by DB-IP.com: free to use AND redistribute,
#   the only condition being attribution. No account, licence key, or contract
#   is required to download it (unlike MaxMind GeoLite2). Even so, PACKTER does
#   NOT bundle the database and does NOT fetch it during build or CI — running
#   this is your deliberate choice. You can equally download it by hand from
#   https://db-ip.com/db/download/ip-to-city-lite
#
# ATTRIBUTION (required by CC BY 4.0)
#   Wherever you display results derived from this data, credit
#   "IP geolocation by DB-IP" with a link to https://db-ip.com/ .
#
# usage: tools/fetch-geoip.sh [output.mmdb]   (default: dbip-city-lite.mmdb)
set -eu

OUT="${1:-dbip-city-lite.mmdb}"
BASE="https://download.db-ip.com/free"

fetch_month() {
    ym="$1"
    if curl -fsSL "$BASE/dbip-city-lite-$ym.mmdb.gz" -o "$OUT.gz"; then
        gunzip -f "$OUT.gz"   # -> $OUT
        return 0
    fi
    rm -f "$OUT.gz"
    return 1
}

# DB-IP publishes one file per month; near the 1st the new month may not be up
# yet, so fall back to the previous month.
this_month="$(date -u +%Y-%m)"
last_month="$(date -u -d 'last month' +%Y-%m 2>/dev/null \
            || date -u -v-1m +%Y-%m 2>/dev/null \
            || echo "$this_month")"

echo "Fetching DB-IP IP-to-City Lite (CC BY 4.0 — attribution required)..."
if fetch_month "$this_month" || fetch_month "$last_month"; then
    echo "Wrote $OUT"
    echo
    echo "  LICENSE: CC BY 4.0. You MUST credit DB-IP wherever you show its data:"
    echo "    \"IP geolocation by DB-IP\"  ->  https://db-ip.com/"
    echo
    echo "  Use it:  pt_agent -v <broker> -i eth0 -G $OUT"
    echo "  Globe:   open the viewer with ?mode=earth"
else
    echo "Download failed. Fetch it manually instead:" >&2
    echo "  https://db-ip.com/db/download/ip-to-city-lite" >&2
    exit 1
fi
