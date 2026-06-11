/*
 * IPFIX / NetFlow v10 (RFC 7011) message reader. Header + Set iteration;
 * template and data decoding are shared with NetFlow v9 in nf_common.c.
 *
 * Differences from v9 handled here:
 *   - 16-byte message header (version 10, total length, observation domain)
 *   - template Set-ID is 2 (options template 3); data sets are id >= 256
 *   - enterprise-bit / variable-length field specifiers (in nf_template_set)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "packter.h"
#include "nf_common.h"

#define IPFIX_TEMPLATE_SET         2
#define IPFIX_OPTIONS_TEMPLATE_SET 3

struct ipfix_header {
    uint16_t version;     /* 10 */
    uint16_t length;      /* total message length in octets */
    uint32_t exporttime;  /* seconds since epoch */
    uint32_t sequence;
    uint32_t domain;      /* observation domain id (analogue of v9 srcid) */
};

void packter_ipfix_read(packter_ctx *ctx, pt_map *templates,
                        const char *buf, int len)
{
    uint32_t domain;
    unsigned version;
    int msglen;

    if (len < (int)sizeof(struct ipfix_header)) {
        return;
    }
    version = pt_be16(buf);              /* ipfix_header.version @ offset 0 */
    if (version != 10) {
        if (ctx->debug == PACKTER_TRUE) {
            printf("ipfix: ignoring datagram version %u (only v10 supported)\n",
                   version);
        }
        return;
    }
    domain = pt_be32(buf + 12);          /* ipfix_header.domain @ offset 12 */

    /* honour the header's declared message length when it is shorter than
     * what we received (trailing bytes / coalesced datagrams) */
    msglen = pt_be16(buf + 2);           /* ipfix_header.length @ offset 2 */
    if (msglen >= (int)sizeof(struct ipfix_header) && msglen < len) {
        len = msglen;
    }

    buf += sizeof(struct ipfix_header);
    len -= (int)sizeof(struct ipfix_header);

    while (len > 0) {
        int setlen;
        unsigned id;

        if (len < (int)sizeof(struct nf_set)) {
            break;
        }
        setlen = pt_be16(buf + 2);       /* nf_set.len */
        if (setlen < (int)sizeof(struct nf_set) || setlen > len) {
            break;
        }
        id = pt_be16(buf);               /* nf_set.id */

        if (id == IPFIX_TEMPLATE_SET) {
            nf_template_set(ctx, templates, buf, setlen, domain, 1);
        } else if (id == IPFIX_OPTIONS_TEMPLATE_SET) {
            /* options template: ignored */
        } else if (id >= 256) {
            nf_flow_set(ctx, templates, buf, setlen, domain);
        }
        buf += setlen;
        len -= setlen;
    }
}
