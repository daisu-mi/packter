/*
 * pt_agent — pcap capture / pcap replay / Snort unsock reader.
 * Option-compatible with PackterAgent 2.5; new: -B (bulk), -g works,
 * IPv6 viewer addresses need no build flag.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "packter.h"

static const char *progname;

static void usage(void)
{
    printf("usage: %s \n", progname);
    printf("      -v [ Viewer IP address (IPv4/IPv6) ]\n");
    printf("      -p [ Viewer Port number ] (optional: default %d)\n", PACKTER_VIEWER_PORT);
    printf("      -i [ Monitor device ] (optional)\n");
    printf("      -r [ Pcap dump file ] (optional)\n");
    printf("      -f [ Flag base ] (optional: default 0)\n");
    printf("      -u [ Run as another username ] (optional)\n");
    printf("      -g [ Run as another groupname ] (optional)\n");
    printf("      -U ( Read from Snort's UNIX domain socket: optional)\n");
    printf("      -S ( Use Snort's alert for viewer: optional)\n");
    printf("      -d ( Show debug information: optional)\n");
    printf("      -R [ Random droprate ] (optional)\n");
    printf("      -T [ Traceback Client ] (optional)\n");
    printf("      -G [ GeoLiteCity datafile ] (optional)\n");
    printf("      -B [ Bulk window ms: pack records into one datagram ] (optional)\n");
    printf("      -s ( enable PACKTERSE: optional)\n");
    printf("      -n ( Not send packter packet: optional)\n");
    printf("      [ pcap filter expression ] (optional)\n");
    printf("      (if -U specified, then [ UNIX domain socket path ]) \n");
    printf("\n");
    printf(" ex) %s -v 192.168.1.1 \"port not 11300 and port not 22\"\n", progname);
    printf("\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    packter_ctx ctx;
    const char *dumpfile = NULL;
    const char *device = NULL;
    const char *filter = NULL;
    const char *ip = NULL;
    const char *user = NULL;
    const char *group = NULL;
    int port = 0;
    int snort = PACKTER_FALSE;
    int op;

    progname = argv[0];
    setvbuf(stdout, NULL, _IONBF, 0);
    packter_ctx_init(&ctx);

    while ((op = getopt(argc, argv, "v:i:r:p:R:T:G:B:f:u:g:nUsSdh?")) != -1) {
        switch (op) {
        case 'f': ctx.flagbase = atoi(optarg); break;
        case 'd': ctx.debug = PACKTER_TRUE; break;
        case 'v': ip = optarg; break;
        case 'p': port = atoi(optarg); break;
        case 'i': device = optarg; break;
        case 'r': dumpfile = optarg; break;
        case 's': ctx.enable_sound = PACKTER_TRUE; break;
        case 'u': user = optarg; break;
        case 'g': group = optarg; break;
        case 'n': ctx.notsend = PACKTER_TRUE; break;
        case 'U': snort = PACKTER_TRUE; break;
        case 'S': ctx.snort_report = PACKTER_TRUE; break;
        case 'R': ctx.rate_limit = atoi(optarg); break;
        case 'B': ctx.bulk_ms = atoi(optarg); break;
        case 'T':
            ctx.trace = PACKTER_TRUE;
            if (strlen(optarg) < 1) {
                usage();
            }
            snprintf(ctx.trace_server, PACKTER_BUFSIZ, "%s", optarg);
            break;
        case 'G':
            ctx.geoip = PACKTER_TRUE;
            if (strlen(optarg) < 1) {
                usage();
            }
            snprintf(ctx.geoip_datfile, PACKTER_BUFSIZ, "%s", optarg);
            break;
        case 'h':
        case '?':
        default:
            usage();
        }
    }

    if (argv[optind] != NULL) {
        filter = argv[optind];
    }
    if (ctx.flagbase < 0) {
        ctx.flagbase = 0;
    }
    if (ctx.rate_limit < 1) {
        ctx.rate_limit = 1;
    }
    if (ctx.bulk_ms < 0) {
        ctx.bulk_ms = 0;
    }
    ctx.rate = packter_rate(ctx.rate_limit);

    if (ctx.notsend == PACKTER_TRUE) {
        ctx.sock = -1;
    } else {
        if (ip == NULL) {
            usage();
        }
        if (packter_connect(&ctx, ip, port) < 0) {
            exit(EXIT_FAILURE);
        }
    }

    packter_drop_privs(user, group);

    if (snort == PACKTER_TRUE) {
        packter_snort(&ctx, filter);
    } else {
        packter_pcap(&ctx, dumpfile, device, filter);
    }
    packter_flush(&ctx);
    return EXIT_SUCCESS;
}
