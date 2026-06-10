/*
 * pt_thmon — threshold monitor. Counts SYN/FIN/RST/ICMP/UDP ratios and
 * PPS over count_max-packet windows (separated by `interval` seconds of
 * quiet) and emits PACKTERMSG / PACKTERSOUND / PACKTERVOICE /
 * PACKTERSKYDOMETEXTURE alerts assembled from packter.conf keys.
 *
 * Intended fixes vs 2.5: MON_OPT_SOUND_HEAD / MON_OPT_SOUND_FOOT are now
 * appended to the sound message (2.5 appended them to the voice buffer).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <pcap.h>

#include "packter.h"

#define PT_THCOUNT 500
#define PT_INTERVAL 30

struct thmon_state {
    int count_max;
    long count_all, count_syn, count_fin, count_rst, count_icmp, count_udp;
    float rate_syn, rate_fin, rate_rst, rate_icmp, rate_udp, rate_pps;
    struct timeval start, stop;
};

static const char *progname;
static packter_ctx g_ctx;
static struct thmon_state th;
static int g_interval = PT_INTERVAL;
static pt_map g_config;
static const char *g_configfile = NULL;

static void usage(void)
{
    printf("usage: %s \n", progname);
    printf("      -v [ Viewer IP address (IPv4/IPv6) ]\n");
    printf("      -p [ Viewer Port number ] (optional: default %d)\n", PACKTER_VIEWER_PORT);
    printf("      -i [ Monitor device ] (optional)\n");
    printf("      -r [ Pcap dump file ] (optional)\n");
    printf("      -d ( Show debug information: optional)\n");
    printf("      -w [ Wait Interval ] (optional: default %d)\n", PT_INTERVAL);
    printf("      -c [ config file ] (optional: default %s)\n", PACKTER_THCONFIG);
    printf("      -s ( Enable Sound: optional: default no)\n");
    printf("      -C [ Number of counting packet ] (optional: default %d)\n", PT_THCOUNT);
    printf("      -S [ TCP SYN Threshold ] (optional)\n");
    printf("      -F [ TCP FIN Threshold ] (optional)\n");
    printf("      -R [ TCP RST Threshold ] (optional)\n");
    printf("      -I [ ICMP Threshold ] (optional)\n");
    printf("      -U [ UDP Threshold ] (optional)\n");
    printf("      -P [ PPS Threshold ] (optional)\n");
    printf("      [ pcap filter expression ] (optional)\n");
    printf("\n");
    exit(EXIT_SUCCESS);
}

static void count_init(void)
{
    th.count_all = th.count_syn = th.count_fin = th.count_rst = 0;
    th.count_icmp = th.count_udp = 0;
}

static int generate_alert(int alert, char *mesg, char *sound, char *voice,
                          const char *mon_pic, const char *mon_mesg,
                          const char *mon_sound, const char *mon_voice,
                          float observed, float threshold)
{
    if (alert == PACKTER_FALSE) {
        if (packter_config_exists(&g_config, mon_pic) == PACKTER_TRUE) {
            packter_addstring_conf(&g_config, mesg, mon_pic);
        }
        packter_addstring(mesg, ",");
        packter_addstring_conf(&g_config, mesg, "MON_OPT_MSG_HEAD");
        packter_addstring_conf(&g_config, sound, "MON_OPT_SOUND_HEAD");
        if (packter_config_exists(&g_config, mon_sound) == PACKTER_TRUE) {
            packter_addstring_conf(&g_config, sound, mon_sound);
        }
        packter_addstring_conf(&g_config, voice, "MON_OPT_VOICE_HEAD");
        alert = PACKTER_TRUE;
    }

    packter_addstring_conf(&g_config, mesg, mon_mesg);
    if (packter_config_exists(&g_config, "MONITOR") == PACKTER_TRUE) {
        packter_addstring_conf(&g_config, mesg, "MONITOR");
    } else {
        packter_addstring(mesg, " Observed:");
    }
    packter_addfloat(mesg, observed);

    if (packter_config_exists(&g_config, "THRESHOLD") == PACKTER_TRUE) {
        packter_addstring_conf(&g_config, mesg, "THRESHOLD");
    } else {
        packter_addstring(mesg, " Threshold:");
    }
    packter_addfloat(mesg, threshold);

    packter_addstring_conf(&g_config, voice, mon_voice);
    return alert;
}

static void analyze(void)
{
    int alert = PACKTER_FALSE;
    char mesg[PACKTER_BUFSIZ];
    char sound[PACKTER_BUFSIZ];
    char voice[PACKTER_BUFSIZ];
    char skydome[PACKTER_BUFSIZ];
    float diff;
    float mon_syn, mon_fin, mon_rst, mon_icmp, mon_udp, mon_pps;

    gettimeofday(&th.stop, NULL);
    mon_syn = (float)th.count_syn / (float)th.count_all;
    mon_fin = (float)th.count_fin / (float)th.count_all;
    mon_rst = (float)th.count_rst / (float)th.count_all;
    mon_icmp = (float)th.count_icmp / (float)th.count_all;
    mon_udp = (float)th.count_udp / (float)th.count_all;

    diff = (float)(th.stop.tv_sec - th.start.tv_sec)
         + (float)(th.stop.tv_usec - th.start.tv_usec) / 1000000.0f;
    if (diff < 1) {
        diff = 1;
    }
    mon_pps = (float)th.count_all / diff;

    snprintf(mesg, sizeof(mesg), "%s", PACKTER_MSG);
    snprintf(sound, sizeof(sound), "%s", PACKTER_SOUND);
    snprintf(voice, sizeof(voice), "%s", PACKTER_VOICE);
    snprintf(skydome, sizeof(skydome), "%s", PACKTER_SKYDOME);

    printf("-------------------------\n");
    printf("Statistics of %ld packet\n", th.count_all);
    printf("Observed: %ld.%ld - %ld.%ld\n",
           (long)th.start.tv_sec, (long)th.start.tv_usec,
           (long)th.stop.tv_sec, (long)th.stop.tv_usec);
    printf("SYN : %.4f   FIN : %.4f   RST: %.4f\n", mon_syn, mon_fin, mon_rst);
    printf("ICMP: %.4f   UDP : %.4f   PPS: %.4f\n", mon_icmp, mon_udp, mon_pps);
    printf("-------------------------\n");

    if (mon_syn > th.rate_syn && th.rate_syn > 0) {
        alert = generate_alert(alert, mesg, sound, voice,
                               "MON_SYN_PIC", "MON_SYN_MSG", "MON_SYN_SOUND", "MON_SYN_VOICE",
                               mon_syn * 100, th.rate_syn * 100);
    }
    if (mon_fin > th.rate_fin && th.rate_fin > 0) {
        alert = generate_alert(alert, mesg, sound, voice,
                               "MON_FIN_PIC", "MON_FIN_MSG", "MON_FIN_SOUND", "MON_FIN_VOICE",
                               mon_fin * 100, th.rate_fin * 100);
    }
    if (mon_rst > th.rate_rst && th.rate_rst > 0) {
        alert = generate_alert(alert, mesg, sound, voice,
                               "MON_RST_PIC", "MON_RST_MSG", "MON_RST_SOUND", "MON_RST_VOICE",
                               mon_rst * 100, th.rate_rst * 100);
    }
    if (mon_icmp > th.rate_icmp && th.rate_icmp > 0) {
        alert = generate_alert(alert, mesg, sound, voice,
                               "MON_ICMP_PIC", "MON_ICMP_MSG", "MON_ICMP_SOUND", "MON_ICMP_VOICE",
                               mon_icmp * 100, th.rate_icmp * 100);
    }
    if (mon_udp > th.rate_udp && th.rate_udp > 0) {
        alert = generate_alert(alert, mesg, sound, voice,
                               "MON_UDP_PIC", "MON_UDP_MSG", "MON_UDP_SOUND", "MON_UDP_VOICE",
                               mon_udp * 100, th.rate_udp * 100);
    }
    if (mon_pps > th.rate_pps && th.rate_pps > 0) {
        alert = generate_alert(alert, mesg, sound, voice,
                               "MON_PPS_PIC", "MON_PPS_MSG", "MON_PPS_SOUND", "MON_PPS_VOICE",
                               mon_pps, th.rate_pps);
    }

    if (alert == PACKTER_TRUE) {
        packter_addstring_conf(&g_config, mesg, "MON_OPT_MSG_FOOT");
        packter_send(&g_ctx, mesg);

        if (g_ctx.enable_sound == PACKTER_TRUE) {
            packter_addstring_conf(&g_config, sound, "MON_OPT_SOUND_FOOT");
            packter_send(&g_ctx, sound);

            packter_addstring_conf(&g_config, voice, "MON_OPT_VOICE_FOOT");
            packter_send(&g_ctx, voice);

            if (packter_config_exists(&g_config, "MON_SKYDOME_START") == PACKTER_TRUE) {
                packter_addstring_conf(&g_config, skydome, "MON_SKYDOME_START");
                packter_send(&g_ctx, skydome);
            }
        }
    }
}

static void count_ip4(const unsigned char *p, unsigned int len)
{
    const struct pt_ip *ip;
    unsigned int hdrlen;

    if (len < sizeof(struct pt_ip)) {
        return;
    }
    ip = (const struct pt_ip *)p;
    if ((ip->vhl >> 4) != 4) {
        return;
    }
    hdrlen = (unsigned int)(ip->vhl & 0x0f) * 4;
    if (hdrlen < sizeof(struct pt_ip) || hdrlen + sizeof(struct pt_tcp) > len) {
        if (ip->proto == 17) { th.count_udp++; }
        if (ip->proto == 1)  { th.count_icmp++; }
        return;
    }
    switch (ip->proto) {
    case 6: {
        const struct pt_tcp *t = (const struct pt_tcp *)(p + hdrlen);
        if (t->flags & PT_TH_SYN) { th.count_syn++; }
        if (t->flags & PT_TH_FIN) { th.count_fin++; }
        if (t->flags & PT_TH_RST) { th.count_rst++; }
        break;
    }
    case 17: th.count_udp++; break;
    case 1:  th.count_icmp++; break;
    default: break;
    }
}

static void thmon_cb(u_char *user, const struct pcap_pkthdr *h, const u_char *p)
{
    const struct pt_ether *ep;
    unsigned int ether_type;
    unsigned int skiplen = PT_ETHER_HDRLEN;

    (void)user;
    if (h->caplen < PT_ETHER_HDRLEN) {
        return;
    }
    ep = (const struct pt_ether *)p;
    ether_type = ntohs(ep->type);
    if (ether_type == PT_ETHERTYPE_8021Q) {
        ep = (const struct pt_ether *)(p + 4);
        ether_type = ntohs(ep->type);
        skiplen += 4;
    }
    if (ether_type != PT_ETHERTYPE_IP && ether_type != PT_ETHERTYPE_IPV6) {
        return;
    }

    if (th.count_all == 0) {
        gettimeofday(&th.start, NULL);
    }
    if (th.start.tv_sec - th.stop.tv_sec > g_interval) {
        th.count_all++;
        if (ether_type == PT_ETHERTYPE_IP) {
            count_ip4(p + skiplen, h->caplen - skiplen);
        }
        /* IPv6: counted in PPS only, as in 2.5 (thmon_ip6 only counted) */
    }
    if (th.count_all >= th.count_max) {
        analyze();
        count_init();
    }
}

static void sig_handler(int sig)
{
    if (sig == SIGHUP) {
        pt_map_clear(&g_config, free);
        if (packter_config_parse(&g_config, g_configfile) < 0) {
            printf("reload configfile failed\n");
        } else {
            printf("reload configfile succeeded\n");
        }
    }
}

int main(int argc, char *argv[])
{
    const char *dumpfile = NULL;
    const char *device = NULL;
    const char *filter = NULL;
    const char *ip = NULL;
    int port = 0;
    int op;
    pcap_t *pd;
    char errbuf[PCAP_ERRBUF_SIZE];
    uint32_t localnet = 0, netmask = 0;
    struct bpf_program fcode;

    progname = argv[0];
    setvbuf(stdout, NULL, _IONBF, 0);
    packter_ctx_init(&g_ctx);
    pt_map_init(&g_config);
    count_init();
    th.count_max = PT_THCOUNT;
    th.rate_syn = th.rate_fin = th.rate_rst = -1;
    th.rate_icmp = th.rate_udp = th.rate_pps = -1;
    th.stop.tv_sec = 0;
    th.stop.tv_usec = 0;
    signal(SIGHUP, sig_handler);

    while ((op = getopt(argc, argv, "v:i:r:p:S:F:R:I:U:P:C:w:c:f:sdh?")) != -1) {
        switch (op) {
        case 'f': g_ctx.flagbase = atoi(optarg); break;
        case 'd': g_ctx.debug = PACKTER_TRUE; break;
        case 'v': ip = optarg; break;
        case 'p': port = atoi(optarg); break;
        case 'i': device = optarg; break;
        case 'r': dumpfile = optarg; break;
        case 'S': th.rate_syn = (float)atof(optarg); break;
        case 'F': th.rate_fin = (float)atof(optarg); break;
        case 'R': th.rate_rst = (float)atof(optarg); break;
        case 'I': th.rate_icmp = (float)atof(optarg); break;
        case 'U': th.rate_udp = (float)atof(optarg); break;
        case 'P': th.rate_pps = (float)atof(optarg); break;
        case 'C': th.count_max = atoi(optarg); break;
        case 'w': g_interval = atoi(optarg); break;
        case 's': g_ctx.enable_sound = PACKTER_TRUE; break;
        case 'c': g_configfile = optarg; break;
        case 'h':
        case '?':
        default:
            usage();
        }
    }

    if (argv[optind] != NULL) {
        filter = argv[optind];
    }
    if (ip == NULL) {
        usage();
    }
    if (packter_config_parse(&g_config, g_configfile) < 0) {
        usage();
    }
    if (th.rate_syn < 0 && th.rate_fin < 0 && th.rate_rst < 0 &&
        th.rate_udp < 0 && th.rate_icmp < 0 && th.rate_pps < 0) {
        printf("*** No threshold is given : %s will not alert to viewer. ***\n", progname);
    }
    if (packter_connect(&g_ctx, ip, port) < 0) {
        exit(EXIT_FAILURE);
    }

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
    if (pcap_loop(pd, -1, thmon_cb, NULL) < 0) {
        fprintf(stderr, "pcap_loop: %s\n", pcap_geterr(pd));
        exit(EXIT_FAILURE);
    }
    pcap_close(pd);
    return EXIT_SUCCESS;
}
