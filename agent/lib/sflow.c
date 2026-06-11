/*
 * sFlow v4 (RFC 3176) flow-sample reader, ported from 2.5 pt_sflow.c.
 * Sampled ethernet frames re-enter the normal fly-mode chain.
 * sFlow v5 / IPFIX ingest is planned on the broker side (phase 5).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pcap.h>

#include "packter.h"

struct sflow_v4_header {
    uint32_t version;
    uint32_t counter_version;
    struct in_addr agent_addr;
    uint32_t seq;
    uint32_t sysuptime;
    uint32_t numsamples;
};

struct sflow_sample {
    uint32_t type;
    uint32_t seq;
    uint32_t id_type;
    uint32_t rate;
    uint32_t pool;
    uint32_t drop;
    uint32_t iif;
    uint32_t oif;
    uint32_t num;
    uint32_t next;
    uint32_t len;
    uint32_t len2;
};

struct sflow_ex_num { uint32_t num; };
struct sflow_ex_type { uint32_t type; };
struct sflow_ex_switch { uint32_t type, in_vlan, in_priority, out_vlan, out_priority; };
struct sflow_ex_router { uint32_t type, num; struct in_addr nexthop; uint32_t src_mask, dst_mask; };

#define SFLOW_EX_SWITCH  1
#define SFLOW_EX_ROUTER  2
#define SFLOW_EX_GATEWAY 3

void packter_sflow_read(packter_ctx *ctx, const char *buf, int len)
{
    int num, i;
    int off = 0;
    uint32_t version;

    if (len < (int)sizeof(struct sflow_v4_header)) {
        return;
    }
    version = pt_be32(buf + offsetof(struct sflow_v4_header, version));
    if (ctx->debug == PACKTER_TRUE) {
        printf("version:%u:%u, seq:%u, uptime:%u, sample:%u\n",
               version,
               pt_be32(buf + offsetof(struct sflow_v4_header, counter_version)),
               pt_be32(buf + offsetof(struct sflow_v4_header, seq)),
               pt_be32(buf + offsetof(struct sflow_v4_header, sysuptime)),
               pt_be32(buf + offsetof(struct sflow_v4_header, numsamples)));
    }
    /* only sFlow v4 is parsed here; v5/other are ignored (broker handles v5) */
    if (version != 4) {
        if (ctx->debug == PACKTER_TRUE) {
            printf("sflow: ignoring datagram version %u (only v4 supported)\n",
                   version);
        }
        return;
    }
    num = (int)pt_be32(buf + offsetof(struct sflow_v4_header, numsamples));
    off += (int)sizeof(struct sflow_v4_header);

    for (i = 0; i < num; i++) {
        struct pcap_pkthdr pkth;
        char mesgbuf[PACKTER_BUFSIZ];
        int samplelen, padding;

        if (off + (int)sizeof(struct sflow_sample) > len) {
            break;
        }
        samplelen = (int)pt_be32(buf + off + offsetof(struct sflow_sample, len));
        padding = (samplelen % 4 > 0) ? 4 - (samplelen % 4) : 0;
        off += (int)sizeof(struct sflow_sample);

        /* compare without overflow: off <= len here, so len - off >= 0 */
        if (samplelen < 0 || samplelen > len - off) {
            break;
        }

        memset(&pkth, 0, sizeof(pkth));
        gettimeofday(&pkth.ts, NULL);
        pkth.caplen = (bpf_u_int32)samplelen;
        pkth.len = (bpf_u_int32)samplelen;

        mesgbuf[0] = '\0';
        packter_ether_frame(ctx, &pkth, (const unsigned char *)(buf + off), mesgbuf);

        off += samplelen + padding;

        /* extension records */
        if (off + (int)sizeof(struct sflow_ex_num) > len) {
            break;
        }
        {
            int exnum = (int)pt_be32(buf + off);   /* sflow_ex_num.num */
            int j;
            off += (int)sizeof(struct sflow_ex_num);

            for (j = 0; j < exnum; j++) {
                if (off + (int)sizeof(struct sflow_ex_type) > len) {
                    break;
                }
                switch (pt_be32(buf + off)) {        /* sflow_ex_type.type */
                case SFLOW_EX_SWITCH:
                    off += (int)sizeof(struct sflow_ex_switch);
                    break;
                case SFLOW_EX_ROUTER:
                case SFLOW_EX_GATEWAY:
                    off += (int)sizeof(struct sflow_ex_router);
                    break;
                default:
                    return; /* unknown extension: cannot resync, stop */
                }
            }
        }
    }
}
