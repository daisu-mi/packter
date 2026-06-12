/*
 * pt_replay — replay a "pt_agent -n" text dump with original timing.
 * TIME lines drive the sleep; everything between two TIME lines is sent
 * as one datagram (which makes replays naturally bulk-framed).
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
    printf("      -v [ Viewer host or IP (IPv4/IPv6) ]\n");
    printf("      -p [ Viewer Port number ] (optional: default %d)\n", PACKTER_VIEWER_PORT);
    printf("      -r [ Text replay file ]\n");
    printf("      -u [ Run as another username ] (optional)\n");
    printf("      -g [ Run as another groupname ] (optional)\n");
    printf("      -d ( Show debug information: optional)\n");
    printf("\n");
    printf(" ex) %s -v 192.168.1.1 -r replay_file\n", progname);
    printf("\n");
    exit(EXIT_SUCCESS);
}

static void replay(packter_ctx *ctx, const char *textfile)
{
    FILE *fp;
    char buf[PACKTER_BUFSIZ];
    char mesg[PACKTER_BUFSIZ_LONG];
    struct timeval now = {0, 0}, text;
    long sec, usec;

    if ((fp = fopen(textfile, "r")) == NULL) {
        fprintf(stderr, "Replay file not found\n");
        exit(EXIT_FAILURE);
    }
    mesg[0] = '\0';

    while (fgets(buf, PACKTER_BUFSIZ, fp) != NULL) {
        if (sscanf(buf, "TIME %ld %ld", &sec, &usec) == 2) {
            if (now.tv_sec == 0 && now.tv_usec == 0) {
                now.tv_sec = sec;
                now.tv_usec = usec;
            } else {
                text.tv_sec = sec;
                text.tv_usec = usec;
                if (mesg[0] != '\0') {
                    packter_send(ctx, mesg);
                    mesg[0] = '\0';
                }
                usleep((useconds_t)(packter_diff_sec(&text, &now) * 1000 * 1000
                                    + packter_diff_usec(&text, &now)));
                now = text;
            }
        } else {
            if (strlen(mesg) + strlen(buf) >= sizeof(mesg)) {
                packter_send(ctx, mesg);
                mesg[0] = '\0';
            }
            strcat(mesg, buf);
        }
    }
    if (mesg[0] != '\0') {
        packter_send(ctx, mesg);
    }
    fclose(fp);
}

int main(int argc, char *argv[])
{
    packter_ctx ctx;
    const char *textfile = NULL;
    const char *ip = NULL;
    const char *user = NULL;
    const char *group = NULL;
    int port = 0;
    int op;

    progname = argv[0];
    setvbuf(stdout, NULL, _IONBF, 0);
    packter_ctx_init(&ctx);

    while ((op = getopt(argc, argv, "v:r:p:u:g:dh?")) != -1) {
        switch (op) {
        case 'd': ctx.debug = PACKTER_TRUE; break;
        case 'v': ip = optarg; break;
        case 'p': port = atoi(optarg); break;
        case 'r': textfile = optarg; break;
        case 'u': user = optarg; break;
        case 'g': group = optarg; break;
        case 'h':
        case '?':
        default:
            usage();
        }
    }

    if (ip == NULL || textfile == NULL) {
        usage();
    }
    if (packter_connect(&ctx, ip, port) < 0) {
        exit(EXIT_FAILURE);
    }
    packter_drop_privs(user, group);
    replay(&ctx, textfile);
    return EXIT_SUCCESS;
}
