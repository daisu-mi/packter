/*
 * Shared NetFlow v9 / IPFIX(v10) template + data-record decoding.
 * Both protocols use the same Set/Template/Field structure and the same
 * Information Element IDs for the fields we care about; only the message
 * header, the template Set-ID, and IPFIX's enterprise/variable-length
 * field specifiers differ. Those differences live in netflow.c / ipfix.c.
 */
#ifndef PT_NF_COMMON_H
#define PT_NF_COMMON_H

#include <stdint.h>
#include "packter.h"

/* Information Element IDs (identical in v9 and IPFIX) */
#define NF_IN_PROTOCOL    4
#define NF_TCP_FLAGS      6
#define NF_L4_SRC_PORT    7
#define NF_IPV4_SRC_ADDR  8
#define NF_L4_DST_PORT    11
#define NF_IPV4_DST_ADDR  12
#define NF_IPV6_SRC_ADDR  27
#define NF_IPV6_DST_ADDR  28
#define NF_ICMP_TYPE      32

#define NF_ABSENT (-1)
#define NF_VARLEN 0xffff   /* IPFIX variable-length field marker */

struct nf_set   { uint16_t id; uint16_t len; };
struct nf_field { uint16_t type; uint16_t len; };

/* per-template field offsets within a flow record */
struct nf_template {
    int l3_src, l3_dst, l4_src, l4_dst;
    int isv6, proto, icmp, tcp_flags;
    int record_len;   /* 0 => unusable (e.g. variable-length IPFIX template) */
};

/* Parse a template set (buf points at the 4-byte set header). key_id is the
 * v9 source-id or the IPFIX observation-domain-id; templates are cached as
 * "key_id-templateid". ipfix != 0 enables enterprise-bit (0x8000) field
 * specifiers (8-byte) and variable-length (0xffff) handling. */
void nf_template_set(packter_ctx *ctx, pt_map *templates,
                     const char *buf, int setlen, uint32_t key_id, int ipfix);

/* Parse a data set (buf points at the 4-byte set header) using a cached
 * template; emits one PACKTER record per flow. Identical for v9 and IPFIX. */
void nf_flow_set(packter_ctx *ctx, pt_map *templates,
                 const char *buf, int setlen, uint32_t key_id);

#endif /* PT_NF_COMMON_H */
