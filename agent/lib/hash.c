/*
 * Traceback packet hash (InterTrack/SPIE style), identical to 2.5:
 * zero out the mutable IPv4 fields (tos/len/off/ttl/sum and any options)
 * or IPv6 traffic class + hop limit, then MD5 over header + first 8
 * payload bytes, emitted as 32 hex chars.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packter.h"
#include "md5.h"

#define PT_IP4_BASE_HDRLEN 20

static void digest_to_hex(const unsigned char digest[16], char *out33)
{
    int i;
    for (i = 0; i < 16; i++) {
        sprintf(out33 + i * 2, "%02x", digest[i]);
    }
}

void packter_hash_ip4(const unsigned char *packet, int len, char *out33)
{
    unsigned char work[PACKTER_BUFSIZ];
    struct pt_ip *ip;
    int hdrlen;
    unsigned char digest[16];

    if (len < (int)sizeof(struct pt_ip) || len > (int)sizeof(work)) {
        out33[0] = '\0';
        return;
    }
    memcpy(work, packet, (size_t)len);
    ip = (struct pt_ip *)work;
    hdrlen = (ip->vhl & 0x0f) * 4;
    if (hdrlen + 8 > len || hdrlen < PT_IP4_BASE_HDRLEN) {
        out33[0] = '\0';
        return;
    }
    if (hdrlen > PT_IP4_BASE_HDRLEN) {
        memset(work + PT_IP4_BASE_HDRLEN, 0, (size_t)(hdrlen - PT_IP4_BASE_HDRLEN));
    }
    ip->tos = 0;
    ip->len = 0;
    ip->off = 0;
    ip->ttl = 0;
    ip->sum = 0;

    pt_md5(work, (size_t)(hdrlen + 8), digest);
    digest_to_hex(digest, out33);
}

void packter_hash_ip6(const unsigned char *packet, int len, char *out33)
{
    unsigned char work[PACKTER_BUFSIZ];
    struct pt_ip6 *ip6;
    unsigned char digest[16];

    if (len < PT_IP6_HDRLEN + 8 || len > (int)sizeof(work)) {
        out33[0] = '\0';
        return;
    }
    memcpy(work, packet, (size_t)len);
    ip6 = (struct pt_ip6 *)work;
    /* 2.5 quirk preserved: only the first byte is masked (version nibble
     * kept, upper traffic-class nibble cleared) and hop limit zeroed —
     * the lower traffic-class bits and flow label stay in the hash */
    work[0] = work[0] & 0xf0;
    ip6->hlim = 0;

    pt_md5(work, (size_t)(PT_IP6_HDRLEN + 8), digest);
    digest_to_hex(digest, out33);
}
