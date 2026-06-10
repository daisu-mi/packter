/*
 * pt_sflow — sFlow v4 collector, forwards sampled frames to the viewer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "packter.h"

static const char *progname;

static void usage(void)
{
    printf("usage: %s \n", progname);
    printf("      -v [ Viewer IP address (IPv4/IPv6) ]\n");
    printf("      -p [ Viewer Port number ] (optional: default %d)\n", PACKTER_VIEWER_PORT);
    printf("      -b [ sFlow Bind IP address ] (optional: default 0.0.0.0)\n");
    printf("      -l [ sFlow Listen port number ] (optional: default %d)\n", PACKTER_SFLOW_PORT);
    printf("      -u [ Run as another username ] (optional)\n");
    printf("      -g [ Run as another groupname ] (optional)\n");
    printf("      -f [ Flag base ] (optional: default 0)\n");
    printf("      -R [ Random droprate ] (optional)\n");
    printf("      -T [ Traceback Client ] (optional)\n");
    printf("      -B [ Bulk window ms ] (optional)\n");
    printf("      -s ( enable PACKTERSE: optional)\n");
    printf("      -n ( Not send packter packet: optional)\n");
    printf("      -d ( Show debug information: optional)\n");
    printf("\n");
    exit(EXIT_SUCCESS);
}

static void serve(packter_ctx *ctx, const char *bind_addr, int bind_port)
{
    int sock;
    struct sockaddr_in server;
    char buf[PACKTER_BUFSIZ_LONG];

    if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((uint16_t)bind_port);
    if (bind_addr != NULL) {
        if (inet_pton(AF_INET, bind_addr, &server.sin_addr) != 1) {
            perror("inet_pton");
            exit(EXIT_FAILURE);
        }
    } else {
        server.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        ssize_t len = recv(sock, buf, sizeof(buf), 0);
        if (len < 1) {
            perror("recv");
            continue;
        }
        packter_sflow_read(ctx, buf, (int)len);
    }
}

int main(int argc, char *argv[])
{
    packter_ctx ctx;
    const char *ip = NULL;
    const char *bind_addr = NULL;
    const char *user = NULL;
    const char *group = NULL;
    int port = 0;
    int bind_port = PACKTER_SFLOW_PORT;
    int op;

    progname = argv[0];
    setvbuf(stdout, NULL, _IONBF, 0);
    packter_ctx_init(&ctx);

    while ((op = getopt(argc, argv, "v:p:b:l:R:T:B:f:u:g:nsdh?")) != -1) {
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
        case 'h':
        case '?':
        default:
            usage();
        }
    }

    if (ctx.rate_limit < 1) {
        ctx.rate_limit = 1;
    }
    ctx.rate = packter_rate(ctx.rate_limit);

    if (ctx.notsend != PACKTER_TRUE) {
        if (ip == NULL) {
            usage();
        }
        if (packter_connect(&ctx, ip, port) < 0) {
            exit(EXIT_FAILURE);
        }
    }
    packter_drop_privs(user, group);
    serve(&ctx, bind_addr, bind_port);
    return EXIT_SUCCESS;
}
