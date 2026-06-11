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
    uint32_t srcid;
    unsigned version;

    if (len < (int)sizeof(struct nf9_header)) {
        return;
    }
    /* only NetFlow v9 here; v5/IPFIX(v10) are ignored */
    version = pt_be16(buf);              /* nf9_header.version @ offset 0 */
    if (version != 9) {
        if (ctx->debug == PACKTER_TRUE) {
            printf("netflow: ignoring datagram version %u (only v9 supported)\n",
                   version);
        }
        return;
    }
    srcid = pt_be32(buf + 16);           /* nf9_header.srcid @ offset 16 */
    buf += sizeof(struct nf9_header);
    len -= (int)sizeof(struct nf9_header);

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
