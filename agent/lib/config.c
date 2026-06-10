/*
 * KEY=VALUE config parser (packter.conf), 2.5-compatible semantics:
 * '#' comments, whitespace trim, first occurrence of a key wins.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "packter.h"

static int config_trim(char *buf)
{
    size_t len = strlen(buf);
    size_t start = 0, i, o;

    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        buf[--len] = '\0';
    }
    while (buf[start] == ' ' || buf[start] == '\t') {
        start++;
    }
    for (i = start, o = 0; buf[i] != '\0'; i++, o++) {
        buf[o] = (buf[i] == '\t') ? ' ' : buf[i];
    }
    buf[o] = '\0';
    return (int)o;
}

int packter_config_parse(pt_map *config, const char *configfile)
{
    FILE *fp;
    char buf[PACKTER_BUFSIZ];
    const char *path = (configfile != NULL) ? configfile : PACKTER_THCONFIG;

    if ((fp = fopen(path, "r")) == NULL) {
        fprintf(stderr, "configuration file %s is not readable\n", path);
        return PACKTER_FALSE;
    }

    while (fgets(buf, PACKTER_BUFSIZ, fp) != NULL) {
        char *sep;
        if (buf[0] == '#') {
            continue;
        }
        if (config_trim(buf) < 1) {
            continue;
        }
        if ((sep = strchr(buf, '=')) == NULL) {
            continue;
        }
        *sep++ = '\0';
        if (strlen(buf) < 1 || strlen(sep) < 1) {
            continue;
        }
        if (pt_map_get(config, buf) == NULL) {
            pt_map_put_once(config, buf, strdup(sep));
        }
    }
    fclose(fp);
    return PACKTER_TRUE;
}

int packter_config_exists(const pt_map *config, const char *key)
{
    const char *val = pt_map_get(config, key);
    if (val == NULL || strlen(val) < 1) {
        return PACKTER_FALSE;
    }
    return PACKTER_TRUE;
}

void packter_addstring(char *buf, const char *val)
{
    char tmp[PACKTER_BUFSIZ];
    if (val == NULL) {
        return;
    }
    snprintf(tmp, PACKTER_BUFSIZ, "%s%s", buf, val);
    memcpy(buf, tmp, PACKTER_BUFSIZ - 1);
    buf[PACKTER_BUFSIZ - 1] = '\0';
}

void packter_addfloat(char *buf, float val)
{
    char tmp[PACKTER_BUFSIZ];
    snprintf(tmp, PACKTER_BUFSIZ, "%s %.f", buf, val);
    memcpy(buf, tmp, PACKTER_BUFSIZ - 1);
    buf[PACKTER_BUFSIZ - 1] = '\0';
}

void packter_addstring_conf(const pt_map *config, char *buf, const char *key)
{
    const char *val;
    if (key == NULL) {
        return;
    }
    val = pt_map_get(config, key);
    if (val != NULL && strlen(val) > 0) {
        packter_addstring(buf, val);
    }
}
