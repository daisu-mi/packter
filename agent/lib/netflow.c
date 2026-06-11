/*
 * NetFlow v9 (RFC 3954) message reader. Header + Set iteration; template
 * and data decoding are shared with IPFIX in nf_common.c.
 *
 * Only v9 is parsed here (v5 / IPFIX are ignored — pt_ipfix handles v10).
 * Field reads are bounds-checked in nf_flow_set.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "packter.h"
#include "nf_common.h"

#define NF9_TEMPLATE_SET     0
#define NF9_OPTIONS_TEMPLATE 1

struct nf9_header {
    uint16_t version;
    uint16_t counter;
    uint32_t sysuptime;
    uint32_t currenttime;
    uint32_t sequence;
    uint32_t srcid;
};

void packter_netflow_read(packter_ctx *ctx, pt_map *templates,
                          const char *buf, int len)
{
    const struct nf9_header *nh;
    uint32_t srcid;

    if (len < (int)sizeof(struct nf9_header)) {
        return;
    }
    nh = (const struct nf9_header *)buf;
    /* only NetFlow v9 here; v5/IPFIX(v10) are ignored */
    if (ntohs(nh->version) != 9) {
        if (ctx->debug == PACKTER_TRUE) {
            printf("netflow: ignoring datagram version %d (only v9 supported)\n",
                   ntohs(nh->version));
        }
        return;
    }
    srcid = ntohl(nh->srcid);
    buf += sizeof(struct nf9_header);
    len -= (int)sizeof(struct nf9_header);

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

        if (id == NF9_TEMPLATE_SET) {
            nf_template_set(ctx, templates, buf, setlen, srcid, 0);
        } else if (id == NF9_OPTIONS_TEMPLATE) {
            /* options template: ignored */
        } else if (id >= 256) {
            nf_flow_set(ctx, templates, buf, setlen, srcid);
        }
        buf += setlen;
        len -= setlen;
    }
}
