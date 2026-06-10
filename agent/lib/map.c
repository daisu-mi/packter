/*
 * Tiny string-keyed map. Replaces glib's GHashTable — the agent keeps at
 * most a few dozen entries (config keys, NetFlow templates), so a linked
 * list is plenty and drops the glib2 build dependency.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packter.h"

void pt_map_init(pt_map *m)
{
    m->head = NULL;
}

void *pt_map_get(const pt_map *m, const char *key)
{
    const pt_kv *e;
    if (key == NULL) {
        return NULL;
    }
    for (e = m->head; e != NULL; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            return e->val;
        }
    }
    return NULL;
}

void pt_map_put_once(pt_map *m, const char *key, void *val)
{
    pt_kv *e;
    if (key == NULL || pt_map_get(m, key) != NULL) {
        return;
    }
    e = malloc(sizeof(*e));
    if (e == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    e->key = strdup(key);
    if (e->key == NULL) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }
    e->val = val;
    e->next = m->head;
    m->head = e;
}

void pt_map_clear(pt_map *m, void (*freeval)(void *))
{
    pt_kv *e = m->head;
    while (e != NULL) {
        pt_kv *next = e->next;
        if (freeval != NULL && e->val != NULL) {
            freeval(e->val);
        }
        free(e->key);
        free(e);
        e = next;
    }
    m->head = NULL;
}
