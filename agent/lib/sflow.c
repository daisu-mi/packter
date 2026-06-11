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
    const struct sflow_v4_header *header;
    int num, i;
    int off = 0;

    if (len < (int)sizeof(struct sflow_v4_header)) {
        return;
    }
    header = (const struct sflow_v4_header *)buf;
    if (ctx->debug == PACKTER_TRUE) {
        printf("version:%d:%d, seq:%d, uptime:%d, sample:%d\n",
               ntohl(header->version), ntohl(header->counter_version),
               ntohl(header->seq), ntohl(header->sysuptime),
               ntohl(header->numsamples));
    }
    /* only sFlow v4 is parsed here; v5/other are ignored (broker handles v5) */
    if (ntohl(header->version) != 4) {
        if (ctx->debug == PACKTER_TRUE) {
            printf("sflow: ignoring datagram version %d (only v4 supported)\n",
                   ntohl(header->version));
        }
        return;
    }
    num = (int)ntohl(header->numsamples);
    off += (int)sizeof(struct sflow_v4_header);

    for (i = 0; i < num; i++) {
        const struct sflow_sample *sample;
        struct pcap_pkthdr pkth;
        char mesgbuf[PACKTER_BUFSIZ];
        int samplelen, padding;

        if (off + (int)sizeof(struct sflow_sample) > len) {
            break;
        }
        sample = (const struct sflow_sample *)(buf + off);
        samplelen = (int)ntohl(sample->len);
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
            const struct sflow_ex_num *ex_num = (const struct sflow_ex_num *)(buf + off);
            int exnum = (int)ntohl(ex_num->num);
            int j;
            off += (int)sizeof(struct sflow_ex_num);

            for (j = 0; j < exnum; j++) {
                const struct sflow_ex_type *ex_type;
                if (off + (int)sizeof(struct sflow_ex_type) > len) {
                    break;
                }
                ex_type = (const struct sflow_ex_type *)(buf + off);
                switch (ntohl(ex_type->type)) {
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
