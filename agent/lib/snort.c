/*
 * Snort "-A unsock" alert reader (UNIX domain datagram socket).
 * Alertpkt layout matches Snort 2.x unified alert struct, as in 2.5.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pcap.h>

#include "packter.h"

#define SNORT_ALERT_MSG_LENGTH 256
#define SNORT_SNAPLEN          1500
#define SNORT_SOCK_NAME        "/var/log/snort/snort_alert"

struct pt_snort_event {
    uint32_t sig_generator;
    uint32_t sig_id;
    uint32_t sig_rev;
    uint32_t classification;
    uint32_t priority;
    uint32_t event_id;
    uint32_t event_reference;
    struct { uint32_t tv_sec; uint32_t tv_usec; } ref_time;
};

struct pt_alertpkt {
    uint8_t alertmsg[SNORT_ALERT_MSG_LENGTH];
    struct pcap_pkthdr pkth;
    uint32_t dlthdr;
    uint32_t nethdr;
    uint32_t transhdr;
    uint32_t data;
    uint32_t val;
    uint8_t pkt[SNORT_SNAPLEN];
    struct pt_snort_event event;
};

void packter_snort(packter_ctx *ctx, const char *sockpath)
{
    int snort_sock;
    struct sockaddr_un un;
    struct pt_alertpkt alert;
    char mesgbuf[PACKTER_BUFSIZ];
    const char *path = (sockpath != NULL) ? sockpath : SNORT_SOCK_NAME;

    if ((snort_sock = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);
    unlink(path);
    if (bind(snort_sock, (struct sockaddr *)&un, sizeof(un)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        if (recv(snort_sock, &alert, sizeof(alert), 0) < 0) {
            perror("recv");
            continue;
        }
        alert.alertmsg[SNORT_ALERT_MSG_LENGTH - 1] = '\0';
        if (ctx->debug == PACKTER_TRUE) {
            printf("incident: %s\n", alert.alertmsg);
        }
        if (ctx->snort_report == PACKTER_TRUE) {
            snprintf(mesgbuf, PACKTER_BUFSIZ, "Incident:%s sid:%lu gen:%lu rev:%lu",
                     alert.alertmsg,
                     (unsigned long)alert.event.sig_id,
                     (unsigned long)alert.event.sig_generator,
                     (unsigned long)alert.event.sig_rev);
        } else {
            mesgbuf[0] = '\0';
        }
        packter_ether_frame(ctx, &alert.pkth, alert.pkt, mesgbuf);
    }
}
