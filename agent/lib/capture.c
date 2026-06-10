/*
 * pcap live/offline capture. ctx travels through pcap's userdata pointer
 * (2.5 passed NULL and used globals — which also made the loopback
 * callback return immediately on its NULL check).
 *
 * Note: the chain consumes caplen, not the wire length. 2.5 passed
 * h->len, which overruns the snaplen-bounded capture buffer for large
 * packets.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcap.h>

#include "packter.h"

static void ether_cb(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
    char mesgbuf[PACKTER_BUFSIZ];
    mesgbuf[0] = '\0';
    packter_ether_frame((packter_ctx *)user, h, p, mesgbuf);
}

static void lback_cb(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
    char mesgbuf[PACKTER_BUFSIZ];
    mesgbuf[0] = '\0';
    packter_lback_frame((packter_ctx *)user, h, p, mesgbuf);
}

void packter_pcap(packter_ctx *ctx, const char *dumpfile, const char *device,
                  const char *filter)
{
    pcap_t *pd;
    char errbuf[PCAP_ERRBUF_SIZE];
    uint32_t localnet = 0, netmask = 0;
    pcap_handler callback;
    struct bpf_program fcode;
    int datalink;

    if (dumpfile != NULL) {
        if ((pd = pcap_open_offline(dumpfile, errbuf)) == NULL) {
            fprintf(stderr, "pcap_open_offline: %s\n", errbuf);
            exit(EXIT_FAILURE);
        }
    } else {
        if (device == NULL) {
            pcap_if_t *alldevs;
            if (pcap_findalldevs(&alldevs, errbuf) < 0 || alldevs == NULL) {
                fprintf(stderr, "pcap_findalldevs: %s\n", errbuf);
                exit(EXIT_FAILURE);
            }
            device = strdup(alldevs->name);
            pcap_freealldevs(alldevs);
        }
        if (ctx->debug == PACKTER_TRUE) {
            printf("device = %s\n", device);
        }
        if ((pd = pcap_open_live(device, PACKTER_SNAPLEN, 1, 500, errbuf)) == NULL) {
            fprintf(stderr, "pcap_open_live: %s\n", errbuf);
            exit(EXIT_FAILURE);
        }
        if (pcap_lookupnet(device, &localnet, &netmask, errbuf) < 0) {
            fprintf(stderr, "pcap_lookupnet: %s\n", errbuf);
        }
    }

    if (pcap_compile(pd, &fcode, filter, 0, netmask) < 0) {
        fprintf(stderr, "pcap_compile: %s\n", pcap_geterr(pd));
        exit(EXIT_FAILURE);
    }
    if (pcap_setfilter(pd, &fcode) < 0) {
        fprintf(stderr, "pcap_setfilter: %s\n", pcap_geterr(pd));
        exit(EXIT_FAILURE);
    }
    pcap_freecode(&fcode);
    if ((datalink = pcap_datalink(pd)) < 0) {
        fprintf(stderr, "pcap_datalink: %s\n", pcap_geterr(pd));
        exit(EXIT_FAILURE);
    }

    switch (datalink) {
    case DLT_NULL:
        if (ctx->debug == PACKTER_TRUE) {
            printf("linktype = LoopBack\n");
        }
        callback = lback_cb;
        break;
    case DLT_EN10MB:
        if (ctx->debug == PACKTER_TRUE) {
            printf("linktype = Ethernet\n");
        }
        callback = ether_cb;
        break;
    default:
        fprintf(stderr, "unsupported linktype %d\n", datalink);
        exit(EXIT_FAILURE);
    }

    if (pcap_loop(pd, -1, callback, (u_char *)ctx) < 0) {
        fprintf(stderr, "pcap_loop: %s\n", pcap_geterr(pd));
        exit(EXIT_FAILURE);
    }
    packter_flush(ctx);
    pcap_close(pd);
}
