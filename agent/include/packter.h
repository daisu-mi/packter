/*
 * PACKTER Agent 3.0 - common definitions
 * Copyright (c) 2008-26 Project PACKTER. BSD 2-Clause.
 *
 * Successor of PackterAgent 2.5. Wire format is unchanged (legacy PACKTER
 * protocol; the broker's compatibility parser documents the accepted forms).
 */
#ifndef PACKTER_H
#define PACKTER_H

#include <stdint.h>
#include <stddef.h>
#include <sys/time.h>
#include <netinet/in.h>

/* Big-endian reads from a possibly-unaligned buffer. Casting a packed wire
 * struct onto an arbitrary datagram offset is undefined behaviour (misaligned
 * access — harmless on x86 but a SIGBUS on strict-alignment CPUs such as ARM),
 * so every multi-byte field parsed out of received packets uses these. */
static inline uint16_t pt_be16(const void *p) {
    const unsigned char *b = (const unsigned char *)p;
    return (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
}
static inline uint32_t pt_be32(const void *p) {
    const unsigned char *b = (const unsigned char *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}

#define PACKTER_VERSION      "3.0.0-beta.1"

#define PACKTER_SNAPLEN      128
#define PACKTER_BUFSIZ       1024
#define PACKTER_BUFSIZ_LONG  16384
#define PACKTER_VIEWER_PORT  11300
#define PACKTER_TC_PORT      11301

#define PACKTER_HEADER  "PACKTER\n"
#define PACKTER_MSG     "PACKTERMSG\n"
#define PACKTER_SOUND   "PACKTERSOUND\n"
#define PACKTER_SE      "PACKTERSE\n"
#define PACKTER_VOICE   "PACKTERVOICE\n"
#define PACKTER_SKYDOME "PACKTERSKYDOMETEXTURE\n"
#define PACKTER_EARTH   "PACKTEARTH\n"
#define PACKTER_TIME    "TIME "

/* flag semantics (viewer maps flag%10 to a color/model) */
#define PACKTER_TCP_ACK   0
#define PACKTER_TCP_SYN   1
#define PACKTER_TCP_FIN   2
#define PACKTER_UDP       3
#define PACKTER_ICMP      4
#define PACKTER_TCP_ACK6  5
#define PACKTER_TCP_SYN6  6
#define PACKTER_TCP_FIN6  7
#define PACKTER_UDP6      8
#define PACKTER_ICMP6     9

#define PACKTER_TRUE   1
#define PACKTER_FALSE  -1

/* bulk send: keep a datagram within a 1500-MTU ethernet payload,
 * leaving headroom for the optional PACKTERAGENT auth line (~100B) */
#define PACKTER_BULK_PAYLOAD 1320

#define PACKTER_AGENT_HEADER "PACKTERAGENT"
#define PACKTER_AGENT_ID_MAX 64
#define PACKTER_PSK_MAX      128

/* -t <proto>: interpret captured UDP payloads as a flow-export protocol and
 * decode them like the pt_sflow/pt_netflow/pt_ipfix collectors */
#define PT_TRANS_NONE    0
#define PT_TRANS_SFLOW   1
#define PT_TRANS_NETFLOW 2
#define PT_TRANS_IPFIX   3

/* ---- runtime context (replaces the 15 extern globals of 2.5) ---- */
typedef struct packter_ctx {
    int debug;
    int notsend;          /* -n: print to stdout instead of sending */
    int use6;             /* viewer address is IPv6 */
    int trace;            /* -T: traceback hash in description */
    int snort_report;     /* -S: overwrite description with snort alert */
    int enable_sound;     /* -s: emit PACKTERSE */
    int geoip;            /* -G: PACKTEARTH mode (needs geoip build) */
    int flagbase;         /* -f */
    int rate_limit;       /* -R */
    int rate;
    int sock;
    struct sockaddr_in  addr;
    struct sockaddr_in6 addr6;
    char trace_server[PACKTER_BUFSIZ];
    char geoip_datfile[PACKTER_BUFSIZ];

    /* -B <ms>: bulk mode. records accumulate and flush as one datagram */
    int bulk_ms;
    char bulk_buf[PACKTER_BULK_PAYLOAD];
    size_t bulk_len;
    struct timeval bulk_first;

    /* -A <id> / -K <pskfile>: PACKTERAGENT identification + HMAC auth */
    char agent_id[PACKTER_AGENT_ID_MAX];
    unsigned char psk[PACKTER_PSK_MAX];
    size_t psk_len;

    /* -t <proto>: translate captured UDP payloads as flow export (PT_TRANS_*) */
    int translate;
    void *translate_templates;   /* pt_map* template cache for netflow/ipfix */
} packter_ctx;

/* read the PSK from the first line of `file` into ctx (lib/send.c) */
void packter_load_psk(packter_ctx *ctx, const char *file);

void packter_ctx_init(packter_ctx *ctx);
int  packter_connect(packter_ctx *ctx, const char *ip, int port);
int  packter_udp_listen(const char *bind_addr, int port);

/* ---- send path (lib/send.c) ---- */
void packter_send(packter_ctx *ctx, const char *mesg);
void packter_flush(packter_ctx *ctx);
void packter_send_se(packter_ctx *ctx, int flag);

/* ---- record formatting (lib/mesg.c) ---- */
void packter_mesg(packter_ctx *ctx, char *mesg, size_t mesglen,
                  const char *srcip, const char *dstip,
                  int data1, int data2, int flag, const char *desc);

/* ---- traceback hash (lib/hash.c, embedded MD5) ---- */
void packter_hash_ip4(const unsigned char *packet, int len, char *out33);
void packter_hash_ip6(const unsigned char *packet, int len, char *out33);

/* ---- util (lib/util.c) ---- */
int  packter_rate(int rate_limit);
unsigned long packter_diff_sec(const struct timeval *a, const struct timeval *b);
unsigned long packter_diff_usec(const struct timeval *a, const struct timeval *b);
void packter_drop_privs(const char *user, const char *group);

/* ---- string map (lib/map.c, replaces glib GHashTable) ---- */
typedef struct pt_kv {
    char *key;
    void *val;
    struct pt_kv *next;
} pt_kv;

typedef struct pt_map {
    pt_kv *head;
} pt_map;

void  pt_map_init(pt_map *m);
void *pt_map_get(const pt_map *m, const char *key);
/* stores val pointer as-is; replaces nothing if key exists (2.5 semantics) */
void  pt_map_put_once(pt_map *m, const char *key, void *val);
void  pt_map_clear(pt_map *m, void (*freeval)(void *));

/* ---- config file KEY=VALUE (lib/config.c) ---- */
#define PACKTER_THCONFIG "/usr/local/etc/packter.conf"
int  packter_config_parse(pt_map *config, const char *configfile);
int  packter_config_exists(const pt_map *config, const char *key);
void packter_addstring(char *buf, const char *val);
void packter_addfloat(char *buf, float val);
void packter_addstring_conf(const pt_map *config, char *buf, const char *key);

/* ---- minimal protocol structs (replaces proto_*.h, packed) ---- */
#define PT_ETHER_HDRLEN  14
#define PT_NULL_HDRLEN   4
#define PT_IP6_HDRLEN    40
#define PT_UDP_HDRLEN    8
#define PT_ICMP_MIN_HDRLEN 2
#define PT_ETHERTYPE_IP     0x0800
#define PT_ETHERTYPE_IPV6   0x86dd
#define PT_ETHERTYPE_8021Q  0x8100
#define PT_TH_FIN 0x01
#define PT_TH_SYN 0x02
#define PT_TH_RST 0x04

struct pt_ether {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

struct pt_ip {
    uint8_t  vhl;          /* version (high 4) + header length (low 4) */
    uint8_t  tos;
    uint16_t len;
    uint16_t id;
    uint16_t off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t sum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed));

struct pt_ip6 {
    uint32_t vtcfl;        /* version + traffic class + flow label */
    uint16_t plen;
    uint8_t  nxt;
    uint8_t  hlim;
    uint8_t  src[16];
    uint8_t  dst[16];
} __attribute__((packed));

struct pt_tcp {
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ack;
    uint8_t  off;
    uint8_t  flags;
    uint16_t win;
    uint16_t sum;
    uint16_t urp;
} __attribute__((packed));

struct pt_udp {
    uint16_t sport;
    uint16_t dport;
    uint16_t len;
    uint16_t sum;
} __attribute__((packed));

struct pt_icmp {
    uint8_t type;
    uint8_t code;
} __attribute__((packed));

/* ---- fly-mode capture chain (lib/proto.c, lib/capture.c) ---- */
struct pcap_pkthdr; /* fwd decl, <pcap.h> in implementation */
void packter_ether_frame(packter_ctx *ctx, const struct pcap_pkthdr *h,
                         const unsigned char *p, char *mesgbuf);
void packter_lback_frame(packter_ctx *ctx, const struct pcap_pkthdr *h,
                         const unsigned char *p, char *mesgbuf);
void packter_ip4(packter_ctx *ctx, const unsigned char *p, unsigned int len, char *mesgbuf);
void packter_ip6(packter_ctx *ctx, const unsigned char *p, unsigned int len, char *mesgbuf);
int  packter_tcp_flag_adjust(int flag, uint8_t tcp_flags);

void packter_pcap(packter_ctx *ctx, const char *dumpfile, const char *device,
                  const char *filter);

/* ---- snort unix socket mode (lib/snort.c) ---- */
void packter_snort(packter_ctx *ctx, const char *sockpath);

/* ---- sFlow v4 / NetFlow v9 / IPFIX(v10) readers ---- */
#define PACKTER_SFLOW_PORT   6343
#define PACKTER_NETFLOW_PORT 2055
#define PACKTER_IPFIX_PORT   4739
void packter_sflow_read(packter_ctx *ctx, const char *buf, int len);
void packter_netflow_read(packter_ctx *ctx, pt_map *templates,
                          const char *buf, int len);
void packter_ipfix_read(packter_ctx *ctx, pt_map *templates,
                        const char *buf, int len);

/* ---- GeoIP (optional, lib/geoip.c stub or real) ---- */
int packter_geoip_lookup(packter_ctx *ctx, const char *host, char *out, size_t outlen);

#endif /* PACKTER_H */
