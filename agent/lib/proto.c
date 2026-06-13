/*
 * Fly-mode packet chain: datalink -> IP -> transport -> PACKTER record.
 * Replaces 2.5's pt_datalink/pt_ip/pt_ip6/pt_tcp/pt_udp/pt_icmp/pt_icmp6
 * (7 near-identical files) with one table-driven dispatcher.
 *
 * Intended fixes vs 2.5 (output otherwise byte-identical):
 *   - sprintf self-overlap UB in the TCP description path (now a temp buf)
 *   - IPv6 dispatched at header+ext-chain offset (2.5 jumped to
 *     p + payload_length, which pointed past the packet for any normal
 *     frame, so IPv6 records were effectively never produced)
 *   - loopback capture works (2.5 passed NULL userdata and bailed out)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>   /* AF_INET/AF_INET6 (not pulled in via arpa/inet.h on *BSD) */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pcap.h>

#include "packter.h"

int packter_tcp_flag_adjust(int flag, uint8_t tcp_flags)
{
    if (tcp_flags & PT_TH_SYN) {
        return flag + (PACKTER_TCP_SYN - PACKTER_TCP_ACK);
    }
    if (tcp_flags & (PT_TH_FIN | PT_TH_RST)) {
        return flag + (PACKTER_TCP_FIN - PACKTER_TCP_ACK);
    }
    return flag;
}

/* description text exactly as 2.5 wrote it; preserves a pre-set snort
 * message by quoting it in front (the 2.5 self-overlap path, fixed) */
static void describe(char *mesgbuf, const char *body)
{
    char tmp[PACKTER_BUFSIZ];
    if (mesgbuf[0] != '\0') {
        snprintf(tmp, PACKTER_BUFSIZ, "\"%s\" %s", mesgbuf, body);
    } else {
        snprintf(tmp, PACKTER_BUFSIZ, "%s", body);
    }
    memcpy(mesgbuf, tmp, PACKTER_BUFSIZ - 1);
    mesgbuf[PACKTER_BUFSIZ - 1] = '\0';
}

static void emit_record(packter_ctx *ctx, const char *srcip, const char *dstip,
                        int data1, int data2, int flag, char *mesgbuf)
{
    char mesg[PACKTER_BUFSIZ];
    packter_mesg(ctx, mesg, sizeof(mesg), srcip, dstip, data1, data2, flag, mesgbuf);
    if (mesg[0] != '\0') {
        packter_send(ctx, mesg);
    }
    if (ctx->enable_sound == PACKTER_TRUE) {
        packter_send_se(ctx, flag);
    }
}

static void handle_tcp(packter_ctx *ctx, const unsigned char *p, unsigned int len,
                       const char *srcip, const char *dstip, int flag, char *mesgbuf)
{
    const struct pt_tcp *th;
    char body[PACKTER_BUFSIZ];

    if (len < sizeof(struct pt_tcp)) {
        return;
    }
    th = (const struct pt_tcp *)p;
    flag = packter_tcp_flag_adjust(flag, th->flags);

    if (ctx->trace == PACKTER_FALSE && ctx->snort_report == PACKTER_FALSE) {
        snprintf(body, sizeof(body), "TCP src:%s(%d) dst:%s(%d)",
                 srcip, ntohs(th->sport), dstip, ntohs(th->dport));
        describe(mesgbuf, body);
    }
    emit_record(ctx, srcip, dstip, ntohs(th->sport), ntohs(th->dport), flag, mesgbuf);
}

static void handle_udp(packter_ctx *ctx, const unsigned char *p, unsigned int len,
                       const char *srcip, const char *dstip, int flag, char *mesgbuf)
{
    const struct pt_udp *uh;

    if (len < PT_UDP_HDRLEN) {
        return;
    }

    /* -t translate: the captured UDP payload IS a flow-export datagram; decode
     * it with the same readers the collectors use (lib/collector.c) and emit
     * flow records instead of one UDP ball.
     *
     * Guard against re-entry: the sFlow reader re-injects each sampled frame
     * through this same chain (packter_ether_frame -> ... -> handle_udp). A
     * sampled UDP frame must become a normal UDP ball, NOT be re-interpreted as
     * yet another flow datagram — so we only translate the outer datagram. */
    if (ctx->translate != PT_TRANS_NONE && ctx->translate_suspend == 0) {
        const char *payload = (const char *)(p + PT_UDP_HDRLEN);
        int paylen = (int)len - PT_UDP_HDRLEN;
        if (paylen > 0) {
            ctx->translate_suspend = 1;
            packter_flow_decode(ctx, ctx->translate,
                                (pt_map *)ctx->translate_templates, payload, paylen);
            ctx->translate_suspend = 0;
        }
        return;
    }

    uh = (const struct pt_udp *)p;
    if (ctx->trace == PACKTER_FALSE && ctx->snort_report == PACKTER_FALSE) {
        snprintf(mesgbuf, PACKTER_BUFSIZ, "UDP src:%s(%d) dst:%s(%d)",
                 srcip, ntohs(uh->sport), dstip, ntohs(uh->dport));
    }
    emit_record(ctx, srcip, dstip, ntohs(uh->sport), ntohs(uh->dport), flag, mesgbuf);
}

static void handle_icmp(packter_ctx *ctx, const unsigned char *p, unsigned int len,
                        const char *srcip, const char *dstip, int flag, char *mesgbuf)
{
    const struct pt_icmp *ih;
    const char *label = (flag == PACKTER_ICMP6) ? "ICMPv6" : "ICMPv4";

    if (len < PT_ICMP_MIN_HDRLEN) {
        return;
    }
    ih = (const struct pt_icmp *)p;
    if (ctx->trace == PACKTER_FALSE && ctx->snort_report == PACKTER_FALSE) {
        snprintf(mesgbuf, PACKTER_BUFSIZ, "%s src:%s dst:%s (type:%d code:%d)",
                 label, srcip, dstip, ih->type, ih->code);
    }
    emit_record(ctx, srcip, dstip, ih->type * 256, ih->code * 256, flag, mesgbuf);
}

void packter_ip4(packter_ctx *ctx, const unsigned char *p, unsigned int len, char *mesgbuf)
{
    const struct pt_ip *ip;
    char srcip[INET_ADDRSTRLEN];
    char dstip[INET_ADDRSTRLEN];
    unsigned int hdrlen;

    if (len < sizeof(struct pt_ip)) {
        return;
    }
    ip = (const struct pt_ip *)p;
    if ((ip->vhl >> 4) != 4) {
        return;
    }
    hdrlen = (unsigned int)(ip->vhl & 0x0f) * 4;
    if (hdrlen < sizeof(struct pt_ip) || hdrlen > len) {
        return;
    }

    inet_ntop(AF_INET, &ip->src, srcip, sizeof(srcip));
    inet_ntop(AF_INET, &ip->dst, dstip, sizeof(dstip));

    if (ctx->trace == PACKTER_TRUE) {
        packter_hash_ip4(p, (int)len, mesgbuf);
    }

    switch (ip->proto) {
    case 6:  /* TCP */
        handle_tcp(ctx, p + hdrlen, len - hdrlen, srcip, dstip, PACKTER_TCP_ACK, mesgbuf);
        break;
    case 17: /* UDP */
        handle_udp(ctx, p + hdrlen, len - hdrlen, srcip, dstip, PACKTER_UDP, mesgbuf);
        break;
    case 1:  /* ICMP */
        handle_icmp(ctx, p + hdrlen, len - hdrlen, srcip, dstip, PACKTER_ICMP, mesgbuf);
        break;
    default:
        break;
    }
}

/* walk IPv6 extension headers to the upper-layer protocol */
static int ip6_upper_layer(const unsigned char *p, unsigned int len,
                           uint8_t *proto, unsigned int *offset)
{
    uint8_t nxt;
    unsigned int off = PT_IP6_HDRLEN;

    if (len < PT_IP6_HDRLEN) {
        return -1;
    }
    nxt = ((const struct pt_ip6 *)p)->nxt;
    for (;;) {
        switch (nxt) {
        case 0:   /* hop-by-hop */
        case 43:  /* routing */
        case 60:  /* destination options */
            if (off + 8 > len) {
                return -1;
            }
            nxt = p[off];
            off += ((unsigned int)p[off + 1] + 1) * 8;
            break;
        case 44:  /* fragment */
            if (off + 8 > len) {
                return -1;
            }
            nxt = p[off];
            off += 8;
            break;
        default:
            *proto = nxt;
            *offset = off;
            return 0;
        }
        if (off >= len) {
            return -1;
        }
    }
}

void packter_ip6(packter_ctx *ctx, const unsigned char *p, unsigned int len, char *mesgbuf)
{
    const struct pt_ip6 *ip6;
    char srcip[INET6_ADDRSTRLEN];
    char dstip[INET6_ADDRSTRLEN];
    uint8_t proto;
    unsigned int off;

    if (len < PT_IP6_HDRLEN) {
        return;
    }
    ip6 = (const struct pt_ip6 *)p;

    if (ctx->trace == PACKTER_TRUE) {
        packter_hash_ip6(p, (int)len, mesgbuf);
    }

    inet_ntop(AF_INET6, ip6->src, srcip, sizeof(srcip));
    inet_ntop(AF_INET6, ip6->dst, dstip, sizeof(dstip));

    if (ip6_upper_layer(p, len, &proto, &off) < 0) {
        return;
    }

    switch (proto) {
    case 6:  /* TCP */
        handle_tcp(ctx, p + off, len - off, srcip, dstip, PACKTER_TCP_ACK6, mesgbuf);
        break;
    case 17: /* UDP */
        handle_udp(ctx, p + off, len - off, srcip, dstip, PACKTER_UDP6, mesgbuf);
        break;
    case 58: /* ICMPv6 */
        handle_icmp(ctx, p + off, len - off, srcip, dstip, PACKTER_ICMP6, mesgbuf);
        break;
    default:
        break;
    }
}

void packter_ether_frame(packter_ctx *ctx, const struct pcap_pkthdr *h,
                         const unsigned char *p, char *mesgbuf)
{
    const struct pt_ether *ep;
    unsigned int ether_type;
    unsigned int skiplen = PT_ETHER_HDRLEN;

    if (h->caplen < PT_ETHER_HDRLEN) {
        return;
    }
    ep = (const struct pt_ether *)p;
    ether_type = ntohs(ep->type);

    if (ether_type == PT_ETHERTYPE_8021Q) {
        if (h->caplen < PT_ETHER_HDRLEN + 4) {
            return;
        }
        ep = (const struct pt_ether *)(p + 4);
        ether_type = ntohs(ep->type);
        skiplen += 4;
    }

    if (h->caplen < skiplen) {
        return;
    }

    switch (ether_type) {
    case PT_ETHERTYPE_IP:
        packter_ip4(ctx, p + skiplen, h->caplen - skiplen, mesgbuf);
        break;
    case PT_ETHERTYPE_IPV6:
        packter_ip6(ctx, p + skiplen, h->caplen - skiplen, mesgbuf);
        break;
    default:
        break;
    }
}

void packter_lback_frame(packter_ctx *ctx, const struct pcap_pkthdr *h,
                         const unsigned char *p, char *mesgbuf)
{
    if (h->caplen < PT_NULL_HDRLEN) {
        return;
    }
    packter_ip4(ctx, p + PT_NULL_HDRLEN, h->caplen - PT_NULL_HDRLEN, mesgbuf);
}
