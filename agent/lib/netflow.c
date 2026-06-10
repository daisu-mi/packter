/*
 * NetFlow v9 (RFC 3954) reader, ported from 2.5 pt_netflow.c.
 * Template sets are cached per (srcid, templateid) and used to locate
 * the L3/L4 fields in subsequent flow sets.
 *
 * Fixes vs 2.5: snprintf-bounded descriptions, template field offset 0
 * no longer confused with "absent" (2.5 used PACKTER_FALSE = -1 as both
 * "absent" and compared tcp_flags offset with == PACKTER_TRUE, so TCP
 * flag coloring only worked when the offset happened to equal 1).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "packter.h"

#define NF9_TEMPLATE_SET 0

#define NF9_IN_PROTOCOL   4
#define NF9_TCP_FLAGS     6
#define NF9_L4_SRC_PORT   7
#define NF9_IPV4_SRC_ADDR 8
#define NF9_L4_DST_PORT   11
#define NF9_IPV4_DST_ADDR 12
#define NF9_IPV6_SRC_ADDR 27
#define NF9_IPV6_DST_ADDR 28
#define NF9_ICMP_TYPE     32

#define NF9_ABSENT (-1)

struct nf9_header {
    uint16_t version;
    uint16_t counter;
    uint32_t sysuptime;
    uint32_t currenttime;
    uint32_t sequence;
    uint32_t srcid;
};

struct nf9_set {
    uint16_t id;
    uint16_t len;
};

struct nf9_field {
    uint16_t type;
    uint16_t len;
};

struct nf9_pointer {
    int l3_src, l3_dst, l4_src, l4_dst;
    int isv6, proto, icmp, tcp_flags;
    int record_len;
};

static void template_set(packter_ctx *ctx, pt_map *templates,
                         const char *buf, int len, uint32_t srcid)
{
    const struct nf9_set *nt;
    struct nf9_pointer *np;
    char key[64];
    int fieldcount, fieldlen, i;
    int off = (int)sizeof(struct nf9_set);

    if (len - off < (int)sizeof(struct nf9_set)) {
        return;
    }
    nt = (const struct nf9_set *)(buf + off);
    off += (int)sizeof(struct nf9_set);

    snprintf(key, sizeof(key), "%u-%u", srcid, (unsigned)ntohs(nt->id));
    if (ctx->debug == PACKTER_TRUE) {
        printf("SET templateid:%u, srcid:%u\n", (unsigned)ntohs(nt->id), srcid);
    }

    np = pt_map_get(templates, key);
    if (np == NULL) {
        np = malloc(sizeof(*np));
        if (np == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        pt_map_put_once(templates, key, np);
    }
    np->l3_src = np->l3_dst = np->l4_src = np->l4_dst = NF9_ABSENT;
    np->proto = np->icmp = np->tcp_flags = NF9_ABSENT;
    np->isv6 = 0;

    fieldcount = ntohs(nt->len);
    fieldlen = 0;
    for (i = 0; i < fieldcount; i++) {
        const struct nf9_field *nf;
        if (len - off < (int)sizeof(struct nf9_field)) {
            break;
        }
        nf = (const struct nf9_field *)(buf + off);
        off += (int)sizeof(struct nf9_field);

        switch (ntohs(nf->type)) {
        case NF9_IPV4_SRC_ADDR: np->l3_src = fieldlen; break;
        case NF9_IPV4_DST_ADDR: np->l3_dst = fieldlen; break;
        case NF9_IPV6_SRC_ADDR: np->l3_src = fieldlen; np->isv6 = 1; break;
        case NF9_IPV6_DST_ADDR: np->l3_dst = fieldlen; np->isv6 = 1; break;
        case NF9_L4_SRC_PORT:   np->l4_src = fieldlen; break;
        case NF9_L4_DST_PORT:   np->l4_dst = fieldlen; break;
        case NF9_ICMP_TYPE:     np->icmp = fieldlen; break;
        case NF9_IN_PROTOCOL:   np->proto = fieldlen; break;
        case NF9_TCP_FLAGS:     np->tcp_flags = fieldlen; break;
        default: break;
        }
        fieldlen += ntohs(nf->len);
    }
    np->record_len = fieldlen;
}

static void flow_set(packter_ctx *ctx, pt_map *templates,
                     const char *buf, int len, uint32_t srcid)
{
    const struct nf9_set *nt;
    const struct nf9_pointer *np;
    char key[64];
    char srcip[INET6_ADDRSTRLEN];
    char dstip[INET6_ADDRSTRLEN];
    char mesg[PACKTER_BUFSIZ];
    char mesgbuf[PACKTER_BUFSIZ];
    int off = (int)sizeof(struct nf9_set);
    int setlen;

    if (len < (int)sizeof(struct nf9_set)) {
        return;
    }
    nt = (const struct nf9_set *)buf;
    setlen = ntohs(nt->len);
    if (setlen > len) {
        setlen = len;
    }

    snprintf(key, sizeof(key), "%u-%u", srcid, (unsigned)ntohs(nt->id));
    if (ctx->debug == PACKTER_TRUE) {
        printf("GET templateid:%u, srcid:%u\n", (unsigned)ntohs(nt->id), srcid);
    }
    np = pt_map_get(templates, key);
    if (np == NULL) {
        return; /* unknown until its template arrives */
    }
    if (np->l3_src == NF9_ABSENT || np->l3_dst == NF9_ABSENT ||
        np->proto == NF9_ABSENT || np->record_len <= 0) {
        printf("unknown netflow\n");
        return;
    }

    /* 2.5 decoded only the first record per set; we walk all of them */
    while (off + np->record_len <= setlen) {
        const char *rec = buf + off;
        uint16_t srcport = 0, dstport = 0;
        uint8_t proto = 0, tcp_flags = 0;
        int flag;

        off += np->record_len;

        if (np->isv6) {
            if (inet_ntop(AF_INET6, rec + np->l3_src, srcip, sizeof(srcip)) == NULL ||
                inet_ntop(AF_INET6, rec + np->l3_dst, dstip, sizeof(dstip)) == NULL) {
                continue;
            }
        } else {
            if (inet_ntop(AF_INET, rec + np->l3_src, srcip, sizeof(srcip)) == NULL ||
                inet_ntop(AF_INET, rec + np->l3_dst, dstip, sizeof(dstip)) == NULL) {
                continue;
            }
        }

        memcpy(&proto, rec + np->proto, 1);
        mesgbuf[0] = '\0';

        switch (proto) {
        case 6: /* TCP */
            if (np->l4_src == NF9_ABSENT || np->l4_dst == NF9_ABSENT) {
                continue;
            }
            memcpy(&srcport, rec + np->l4_src, 2);
            memcpy(&dstport, rec + np->l4_dst, 2);
            flag = np->isv6 ? PACKTER_TCP_ACK6 : PACKTER_TCP_ACK;
            if (np->tcp_flags != NF9_ABSENT) {
                memcpy(&tcp_flags, rec + np->tcp_flags, 1);
                flag = packter_tcp_flag_adjust(flag, tcp_flags);
            }
            snprintf(mesgbuf, PACKTER_BUFSIZ, "TCP src:%s(%d) dst:%s(%d)",
                     srcip, ntohs(srcport), dstip, ntohs(dstport));
            break;

        case 17: /* UDP */
            if (np->l4_src == NF9_ABSENT || np->l4_dst == NF9_ABSENT) {
                continue;
            }
            memcpy(&srcport, rec + np->l4_src, 2);
            memcpy(&dstport, rec + np->l4_dst, 2);
            flag = np->isv6 ? PACKTER_UDP6 : PACKTER_UDP;
            snprintf(mesgbuf, PACKTER_BUFSIZ, "UDP src:%s(%d) dst:%s(%d)",
                     srcip, ntohs(srcport), dstip, ntohs(dstport));
            break;

        case 1:  /* ICMP */
        case 58: /* ICMPv6 */
            if (np->icmp == NF9_ABSENT) {
                continue;
            }
            memcpy(&srcport, rec + np->icmp, 2);
            dstport = srcport;
            flag = (proto == 58) ? PACKTER_ICMP6 : PACKTER_ICMP;
            snprintf(mesgbuf, PACKTER_BUFSIZ, "%s src:%s dst:%s (type:%d code:%d)",
                     (proto == 58) ? "ICMPv6" : "ICMPv4",
                     srcip, dstip, ntohs(srcport) / 256, ntohs(srcport) % 256);
            break;

        default:
            if (ctx->debug == PACKTER_TRUE) {
                printf("unknown protocol: %d\n", proto);
            }
            continue;
        }

        packter_mesg(ctx, mesg, sizeof(mesg), srcip, dstip,
                     ntohs(srcport), ntohs(dstport), flag, mesgbuf);
        if (mesg[0] != '\0') {
            packter_send(ctx, mesg);
        }
        if (ctx->enable_sound == PACKTER_TRUE) {
            packter_send_se(ctx, flag);
        }
    }
}

void packter_netflow_read(packter_ctx *ctx, pt_map *templates,
                          const char *buf, int len)
{
    const struct nf9_header *nh;
    uint32_t srcid;

    if (len < (int)sizeof(struct nf9_header)) {
        return;
    }
    nh = (const struct nf9_header *)buf;
    srcid = ntohl(nh->srcid);
    buf += sizeof(struct nf9_header);
    len -= (int)sizeof(struct nf9_header);

    while (len > 0) {
        const struct nf9_set *nt;
        int setlen;

        if (len < (int)sizeof(struct nf9_set)) {
            break;
        }
        nt = (const struct nf9_set *)buf;
        setlen = ntohs(nt->len);
        if (setlen < (int)sizeof(struct nf9_set) || setlen > len) {
            break;
        }

        if (ntohs(nt->id) == NF9_TEMPLATE_SET) {
            template_set(ctx, templates, buf, setlen, srcid);
        } else if (ntohs(nt->id) >= 256) {
            flow_set(ctx, templates, buf, setlen, srcid);
        }
        buf += setlen;
        len -= setlen;
    }
}
