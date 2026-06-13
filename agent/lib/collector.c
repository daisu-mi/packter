/*
 * Shared flow-export layer for pt_sflow / pt_netflow / pt_ipfix and for
 * pt_agent's -t (translate) mode.
 *
 * Two pieces of duplication used to live across the three collector mains and
 * proto.c's translate hook:
 *   1. the protocol -> reader dispatch (which decoder handles this datagram),
 *   2. the collector scaffolding (argv parsing, viewer connect, dual-stack
 *      UDP listen, recv loop).
 * Both are centralised here. The collectors are now thin mains that call
 * packter_collector_run(); pt_agent -t calls packter_flow_decode() with the
 * payload it pulled out of a captured frame. A flow datagram is therefore
 * decoded by identical code whether it arrived on a collector socket or was
 * sniffed off the wire.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "packter.h"

void packter_flow_decode(packter_ctx *ctx, int proto, pt_map *templates,
                         const char *buf, int len)
{
    switch (proto) {
    case PT_TRANS_SFLOW:
        packter_sflow_read(ctx, buf, len);
        break;
    case PT_TRANS_NETFLOW:
        packter_netflow_read(ctx, templates, buf, len);
        break;
    case PT_TRANS_IPFIX:
        packter_ipfix_read(ctx, templates, buf, len);
        break;
    default:
        break;
    }
}

static const char *progname;

static void usage(const char *name, int default_port, int has_trace)
{
    printf("usage: %s \n", progname);
    printf("      -v [ Viewer host or IP (IPv4/IPv6) ]\n");
    printf("      -p [ Viewer Port number ] (optional: default %d)\n", PACKTER_VIEWER_PORT);
    printf("      -b [ %s Bind IP, v4/v6 ] (optional: default 127.0.0.1 loopback; 0.0.0.0 or :: to expose)\n", name);
    printf("      -l [ %s Listen port number ] (optional: default %d)\n", name, default_port);
    printf("      -u [ Run as another username ] (optional)\n");
    printf("      -g [ Run as another groupname ] (optional)\n");
    printf("      -f [ Flag base ] (optional: default 0)\n");
    printf("      -R [ Random droprate ] (optional)\n");
    if (has_trace) {
        printf("      -T [ Traceback Client ] (optional)\n");
    }
    printf("      -B [ Bulk window ms ] (optional)\n");
    printf("      -s ( enable PACKTERSE: optional)\n");
    printf("      -n ( Not send packter packet: optional)\n");
    printf("      -d ( Show debug information: optional)\n");
    printf("      -A [ Agent ID: adds PACKTERAGENT line ] (optional)\n");
    printf("      -K [ PSK file: HMAC-SHA256 auth, requires -A ] (optional)\n");
    printf("\n");
    exit(EXIT_SUCCESS);
}

int packter_collector_run(int argc, char **argv, int proto,
                          const char *name, int default_port, int has_trace)
{
    packter_ctx ctx;
    pt_map templates;
    const char *ip = NULL;
    const char *bind_addr = "127.0.0.1";   /* secure default: loopback; -b to expose */
    const char *user = NULL;
    const char *group = NULL;
    int port = 0;
    int bind_port = default_port;
    int sock, op;
    char buf[PACKTER_BUFSIZ_LONG];

    /* sFlow accepts -T (the sampled frame is a real packet, so traceback
     * hashing applies); netflow/ipfix have no raw frame to hash. */
    const char *optstr = has_trace ? "v:p:b:l:R:T:B:f:u:g:A:K:nsdh?"
                                   : "v:p:b:l:R:B:f:u:g:A:K:nsdh?";

    progname = argv[0];
    setvbuf(stdout, NULL, _IONBF, 0);
    packter_ctx_init(&ctx);
    pt_map_init(&templates);

    while ((op = getopt(argc, argv, optstr)) != -1) {
        switch (op) {
        case 'f': ctx.flagbase = atoi(optarg); break;
        case 'd': ctx.debug = PACKTER_TRUE; break;
        case 'v': ip = optarg; break;
        case 'p': port = atoi(optarg); break;
        case 'b': bind_addr = optarg; break;
        case 'l': bind_port = atoi(optarg); break;
        case 's': ctx.enable_sound = PACKTER_TRUE; break;
        case 'u': user = optarg; break;
        case 'g': group = optarg; break;
        case 'n': ctx.notsend = PACKTER_TRUE; break;
        case 'R': ctx.rate_limit = atoi(optarg); break;
        case 'B': ctx.bulk_ms = atoi(optarg); break;
        case 'T':
            ctx.trace = PACKTER_TRUE;
            snprintf(ctx.trace_server, PACKTER_BUFSIZ, "%s", optarg);
            break;
        case 'A': /* agent id (PACKTERAGENT line) */
            snprintf(ctx.agent_id, sizeof(ctx.agent_id), "%s", optarg);
            break;
        case 'K': /* PSK file for HMAC auth */
            packter_load_psk(&ctx, optarg);
            break;
        case 'h':
        case '?':
        default:
            usage(name, default_port, has_trace);
        }
    }

    if (ctx.psk_len > 0 && ctx.agent_id[0] == '\0') {
        fprintf(stderr, "-K requires -A <agent id>\n");
        exit(EXIT_FAILURE);
    }
    if (ctx.rate_limit < 1) {
        ctx.rate_limit = 1;
    }
    ctx.rate = packter_rate(ctx.rate_limit);

    if (ctx.notsend != PACKTER_TRUE) {
        if (ip == NULL) {
            usage(name, default_port, has_trace);
        }
        if (packter_connect(&ctx, ip, port) < 0) {
            exit(EXIT_FAILURE);
        }
    }
    packter_drop_privs(user, group);

    sock = packter_udp_listen(bind_addr, bind_port);
    for (;;) {
        ssize_t len = recv(sock, buf, sizeof(buf), 0);
        if (len < 1) {
            perror("recv");
            continue;
        }
        packter_flow_decode(&ctx, proto, &templates, buf, (int)len);
    }
    return EXIT_SUCCESS;   /* not reached */
}
