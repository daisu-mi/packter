/*
 * Shared NetFlow v9 / IPFIX template + data decoding. See nf_common.h.
 * Safety: all template-driven reads are bounds-checked against record_len
 * (a malicious template cannot make us read past a record).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>   /* AF_INET/AF_INET6 (not pulled in via arpa/inet.h on *BSD) */
#include <netinet/in.h>
#include <arpa/inet.h>

#include "packter.h"
#include "nf_common.h"

void nf_template_set(packter_ctx *ctx, pt_map *templates,
                     const char *buf, int setlen, uint32_t key_id, int ipfix)
{
    struct nf_template *np;
    char key[64];
    unsigned template_id;
    int fieldcount, fieldlen, variable, i;
    int off = (int)sizeof(struct nf_set);

    if (setlen - off < (int)sizeof(struct nf_set)) {
        return;
    }
    template_id = pt_be16(buf + off);          /* (template_id, field_count) */
    fieldcount  = pt_be16(buf + off + 2);
    off += (int)sizeof(struct nf_set);

    snprintf(key, sizeof(key), "%u-%u", key_id, template_id);
    if (ctx->debug == PACKTER_TRUE) {
        printf("%s SET templateid:%u, id:%u\n",
               ipfix ? "ipfix" : "netflow", template_id, key_id);
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
    np->l3_src = np->l3_dst = np->l4_src = np->l4_dst = NF_ABSENT;
    np->proto = np->icmp = np->tcp_flags = NF_ABSENT;
    np->isv6 = 0;

    fieldlen = 0;
    variable = 0;
    for (i = 0; i < fieldcount; i++) {
        uint16_t ie, flen;

        if (setlen - off < (int)sizeof(struct nf_field)) {
            break;
        }
        ie = pt_be16(buf + off);
        flen = pt_be16(buf + off + 2);
        off += (int)sizeof(struct nf_field);

        /* IPFIX enterprise-specific field: a 4-byte enterprise number
         * follows the specifier and the IE is non-standard (skip mapping) */
        if (ipfix && (ie & 0x8000)) {
            if (setlen - off < 4) {
                break;
            }
            off += 4;
            if (flen == NF_VARLEN) {
                variable = 1;
            } else {
                fieldlen += flen;
            }
            continue;
        }

        if (flen == NF_VARLEN) {
            variable = 1;   /* variable-length record: fixed offsets unknown */
        }

        switch (ie) {   /* offset of this field = fieldlen so far */
        case NF_IPV4_SRC_ADDR: np->l3_src = fieldlen; break;
        case NF_IPV4_DST_ADDR: np->l3_dst = fieldlen; break;
        case NF_IPV6_SRC_ADDR: np->l3_src = fieldlen; np->isv6 = 1; break;
        case NF_IPV6_DST_ADDR: np->l3_dst = fieldlen; np->isv6 = 1; break;
        case NF_L4_SRC_PORT:   np->l4_src = fieldlen; break;
        case NF_L4_DST_PORT:   np->l4_dst = fieldlen; break;
        case NF_ICMP_TYPE:     np->icmp = fieldlen; break;
        case NF_IN_PROTOCOL:   np->proto = fieldlen; break;
        case NF_TCP_FLAGS:     np->tcp_flags = fieldlen; break;
        default: break;
        }

        if (flen != NF_VARLEN) {
            fieldlen += flen;
        }
    }

    /* a variable-length template breaks the fixed-offset model; mark unusable */
    np->record_len = variable ? 0 : fieldlen;
}

void nf_flow_set(packter_ctx *ctx, pt_map *templates,
                 const char *buf, int setlen, uint32_t key_id)
{
    const struct nf_template *np;
    char key[64];
    char srcip[INET6_ADDRSTRLEN];
    char dstip[INET6_ADDRSTRLEN];
    char mesg[PACKTER_BUFSIZ];
    char mesgbuf[PACKTER_BUFSIZ];
    int off = (int)sizeof(struct nf_set);

    if (setlen < (int)sizeof(struct nf_set)) {
        return;
    }

    snprintf(key, sizeof(key), "%u-%u", key_id, (unsigned)pt_be16(buf));
    np = pt_map_get(templates, key);
    if (np == NULL) {
        return;   /* unknown until its template arrives */
    }
    if (np->l3_src == NF_ABSENT || np->l3_dst == NF_ABSENT ||
        np->proto == NF_ABSENT || np->record_len <= 0) {
        if (ctx->debug == PACKTER_TRUE) {
            printf("flow: template %s not usable\n", key);
        }
        return;
    }

/* a field is safe to read only if offset + read size stays in the record */
#define FLD_OK(o, sz) ((o) != NF_ABSENT && (o) >= 0 && (o) + (sz) <= np->record_len)

    while (off + np->record_len <= setlen) {
        const char *rec = buf + off;
        uint16_t srcport = 0, dstport = 0;
        uint8_t proto = 0, tcp_flags = 0;
        int ipsz = np->isv6 ? 16 : 4;
        int flag;

        off += np->record_len;

        if (!FLD_OK(np->l3_src, ipsz) || !FLD_OK(np->l3_dst, ipsz) ||
            !FLD_OK(np->proto, 1)) {
            continue;
        }

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
            if (!FLD_OK(np->l4_src, 2) || !FLD_OK(np->l4_dst, 2)) {
                continue;
            }
            memcpy(&srcport, rec + np->l4_src, 2);
            memcpy(&dstport, rec + np->l4_dst, 2);
            flag = np->isv6 ? PACKTER_TCP_ACK6 : PACKTER_TCP_ACK;
            if (FLD_OK(np->tcp_flags, 1)) {
                memcpy(&tcp_flags, rec + np->tcp_flags, 1);
                flag = packter_tcp_flag_adjust(flag, tcp_flags);
            }
            snprintf(mesgbuf, PACKTER_BUFSIZ, "TCP src:%s(%d) dst:%s(%d)",
                     srcip, ntohs(srcport), dstip, ntohs(dstport));
            break;

        case 17: /* UDP */
            if (!FLD_OK(np->l4_src, 2) || !FLD_OK(np->l4_dst, 2)) {
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
            if (!FLD_OK(np->icmp, 2)) {
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
#undef FLD_OK
}
