#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include "packter.h"

int packter_rate(int rate_limit)
{
    if (rate_limit < 1) {
        return 1;
    }
    return rand() % rate_limit + 1;
}

unsigned long packter_diff_sec(const struct timeval *a, const struct timeval *b)
{
    if (a->tv_sec < b->tv_sec) {
        return 0;
    }
    if (a->tv_sec == b->tv_sec) {
        return 0;
    }
    if (a->tv_usec <= b->tv_usec) {
        return (unsigned long)(a->tv_sec - b->tv_sec - 1);
    }
    return (unsigned long)(a->tv_sec - b->tv_sec);
}

unsigned long packter_diff_usec(const struct timeval *a, const struct timeval *b)
{
    if (a->tv_sec < b->tv_sec) {
        return 0;
    }
    if (a->tv_sec == b->tv_sec) {
        if (a->tv_usec <= b->tv_usec) {
            return 0;
        }
        return (unsigned long)(a->tv_usec - b->tv_usec);
    }
    if (a->tv_usec <= b->tv_usec) {
        return (unsigned long)(1000 * 1000 + a->tv_usec - b->tv_usec);
    }
    return (unsigned long)(a->tv_usec - b->tv_usec);
}

/*
 * Shared privilege drop (was copy-pasted into 4 tools in 2.5).
 * Group must be set before user; both optional.
 */
void packter_drop_privs(const char *user, const char *group)
{
    if (group != NULL) {
        struct group *gr = getgrnam(group);
        if (gr == NULL) {
            fprintf(stderr, "unknown groupname: %s\n", group);
            exit(EXIT_FAILURE);
        }
        if (setgid(gr->gr_gid) < 0) {
            perror("setgid");
            exit(EXIT_FAILURE);
        }
    }
    if (user != NULL) {
        struct passwd *pw = getpwnam(user);
        if (pw == NULL) {
            fprintf(stderr, "unknown username: %s\n", user);
            exit(EXIT_FAILURE);
        }
        if (group == NULL && setgid(pw->pw_gid) < 0) {
            perror("setgid");
            exit(EXIT_FAILURE);
        }
        if (setuid(pw->pw_uid) < 0) {
            perror("setuid");
            exit(EXIT_FAILURE);
        }
    }
}
