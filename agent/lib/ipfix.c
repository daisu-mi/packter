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
    const struct ipfix_header *ih;
    uint32_t domain;
    int msglen;

    if (len < (int)sizeof(struct ipfix_header)) {
        return;
    }
    ih = (const struct ipfix_header *)buf;
    if (ntohs(ih->version) != 10) {
        if (ctx->debug == PACKTER_TRUE) {
            printf("ipfix: ignoring datagram version %d (only v10 supported)\n",
                   ntohs(ih->version));
        }
        return;
    }
    domain = ntohl(ih->domain);

    /* honour the header's declared message length when it is shorter than
     * what we received (trailing bytes / coalesced datagrams) */
    msglen = ntohs(ih->length);
    if (msglen >= (int)sizeof(struct ipfix_header) && msglen < len) {
        len = msglen;
    }

    buf += sizeof(struct ipfix_header);
    len -= (int)sizeof(struct ipfix_header);

    while (len > 0) {
        const struct nf_set *nt;
        int setlen;
        unsigned id;

        if (len < (int)sizeof(struct nf_set)) {
            break;
        }
        nt = (const struct nf_set *)buf;
        setlen = ntohs(nt->len);
        if (setlen < (int)sizeof(struct nf_set) || setlen > len) {
            break;
        }
        id = ntohs(nt->id);

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
