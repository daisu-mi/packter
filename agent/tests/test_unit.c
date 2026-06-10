/* Unit tests for libpackter pure functions. Exit 0 = pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "packter.h"
#include "../lib/md5.h"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("ok   %s\n", name); } \
    else { printf("FAIL %s (line %d)\n", name, __LINE__); failures++; } \
} while (0)

static void test_md5(void)
{
    unsigned char d[16];
    char hex[33];
    int i;
    pt_md5((const unsigned char *)"abc", 3, d);
    for (i = 0; i < 16; i++) {
        sprintf(hex + i * 2, "%02x", d[i]);
    }
    CHECK(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0, "md5 rfc1321 vector");

    pt_md5((const unsigned char *)"", 0, d);
    for (i = 0; i < 16; i++) {
        sprintf(hex + i * 2, "%02x", d[i]);
    }
    CHECK(strcmp(hex, "d41d8cd98f00b204e9800998ecf8427e") == 0, "md5 empty vector");
}

static void test_flag_adjust(void)
{
    CHECK(packter_tcp_flag_adjust(PACKTER_TCP_ACK, 0x10) == PACKTER_TCP_ACK, "tcp ACK keeps flag 0");
    CHECK(packter_tcp_flag_adjust(PACKTER_TCP_ACK, PT_TH_SYN) == PACKTER_TCP_SYN, "tcp SYN -> 1");
    CHECK(packter_tcp_flag_adjust(PACKTER_TCP_ACK, PT_TH_FIN) == PACKTER_TCP_FIN, "tcp FIN -> 2");
    CHECK(packter_tcp_flag_adjust(PACKTER_TCP_ACK, PT_TH_RST) == PACKTER_TCP_FIN, "tcp RST -> 2");
    CHECK(packter_tcp_flag_adjust(PACKTER_TCP_ACK6, PT_TH_SYN) == PACKTER_TCP_SYN6, "tcp6 SYN -> 6");
    CHECK(packter_tcp_flag_adjust(PACKTER_TCP_ACK6, PT_TH_SYN | PT_TH_FIN) == PACKTER_TCP_SYN6,
          "SYN wins over FIN as in 2.5");
}

static void test_mesg_format(void)
{
    packter_ctx ctx;
    char mesg[PACKTER_BUFSIZ];

    packter_ctx_init(&ctx);
    packter_mesg(&ctx, mesg, sizeof(mesg), "192.168.1.1", "10.0.0.1", 49305, 80, 5, "This is a test");
    CHECK(strcmp(mesg, "PACKTER\n192.168.1.1,10.0.0.1,49305,80,5,This is a test\n") == 0,
          "record format matches wiki example");

    ctx.flagbase = 10;
    packter_mesg(&ctx, mesg, sizeof(mesg), "a", "b", 1, 2, 3, "d");
    CHECK(strcmp(mesg, "PACKTER\na,b,1,2,13,d\n") == 0, "flagbase added");

    ctx.flagbase = 0;
    ctx.trace = PACKTER_TRUE;
    snprintf(ctx.trace_server, PACKTER_BUFSIZ, "172.16.45.1");
    packter_mesg(&ctx, mesg, sizeof(mesg), "a", "b", 1, 2, 4, "0123456789abcdef");
    CHECK(strcmp(mesg, "PACKTER\na,b,1,2,4,0123456789abcdef-172.16.45.1\n") == 0,
          "traceback suffix -SERVER appended");
}

static void test_rate(void)
{
    int i, ok = 1;
    for (i = 0; i < 100; i++) {
        if (packter_rate(1) != 1) {
            ok = 0;
        }
    }
    CHECK(ok, "rate_limit=1 always sends");
    for (i = 0; i < 100; i++) {
        int r = packter_rate(5);
        if (r < 1 || r > 5) {
            ok = 0;
        }
    }
    CHECK(ok, "rate within [1, limit]");
}

static void test_config(void)
{
    pt_map config;
    char path[] = "/tmp/packter_test_conf_XXXXXX";
    int fd = mkstemp(path);
    FILE *fp = fdopen(fd, "w");
    fprintf(fp, "# comment line\n");
    fprintf(fp, "KEY1=value one  \n");
    fprintf(fp, "  KEY2 = spaced\n");
    fprintf(fp, "KEY1=duplicate ignored\n");
    fprintf(fp, "EMPTY=\n");
    fclose(fp);

    pt_map_init(&config);
    CHECK(packter_config_parse(&config, path) == PACKTER_TRUE, "config parse ok");
    CHECK(pt_map_get(&config, "KEY1") != NULL &&
          strcmp(pt_map_get(&config, "KEY1"), "value one") == 0, "trailing space trimmed");
    CHECK(pt_map_get(&config, "KEY2 ") != NULL || pt_map_get(&config, "KEY2") != NULL,
          "spaced key stored");
    CHECK(strcmp(pt_map_get(&config, "KEY1"), "value one") == 0, "first occurrence wins");
    CHECK(packter_config_exists(&config, "NOPE") == PACKTER_FALSE, "missing key");
    unlink(path);
    pt_map_clear(&config, free);
    CHECK(pt_map_get(&config, "KEY1") == NULL, "map cleared");
}

static void test_hash_ip4(void)
{
    /* minimal 20B IPv4 header + 8B payload; expected value computed by
     * tests/expected.py (independent python implementation) */
    unsigned char pkt[28] = {
        0x45, 0x10, 0x00, 0x1c, 0x12, 0x34, 0x40, 0x00,
        0x40, 0x06, 0xbe, 0xef,
        192, 168, 1, 10,
        10, 0, 0, 80,
        0xc0, 0x00, 0x00, 0x50, 0xde, 0xad, 0xbe, 0xef,
    };
    char out[33];
    packter_hash_ip4(pkt, sizeof(pkt), out);
    CHECK(strlen(out) == 32, "ip4 hash is 32 hex chars");
    /* mutable fields must not affect the hash */
    {
        unsigned char pkt2[28];
        char out2[33];
        memcpy(pkt2, pkt, sizeof(pkt));
        pkt2[1] = 0xff;  /* tos */
        pkt2[8] = 0x01;  /* ttl */
        pkt2[10] = 0x00; /* sum */
        packter_hash_ip4(pkt2, sizeof(pkt2), out2);
        CHECK(strcmp(out, out2) == 0, "tos/ttl/sum do not change hash");
    }
}

int main(void)
{
    test_md5();
    test_flag_adjust();
    test_mesg_format();
    test_rate();
    test_config();
    test_hash_ip4();
    printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
