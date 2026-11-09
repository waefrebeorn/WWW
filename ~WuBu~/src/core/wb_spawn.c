/*
 * wb_spawn.c -- spawn points. See wb_spawn.h.
 */
#include "wb_spawn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wb_spawn_set {
    wb_spawn_point *pts;
    int count;
    int cap;
};

wb_spawn_set *wb_spawn_new(void) {
    wb_spawn_set *s = (wb_spawn_set *)calloc(1, sizeof *s);
    return s;
}

void wb_spawn_free(wb_spawn_set *s) {
    if (!s) return;
    free(s->pts);
    free(s);
}

int wb_spawn_add(wb_spawn_set *s, const float pos[3], int team,
                 const char *tag, const char *gamemode) {
    if (!s || !pos) return -1;
    if (s->count >= WB_SPAWN_MAX_POINTS) return -1;
    if (s->count >= s->cap) {
        int nc = s->cap ? s->cap * 2 : 16;
        if (nc > WB_SPAWN_MAX_POINTS) nc = WB_SPAWN_MAX_POINTS;
        wb_spawn_point *np = (wb_spawn_point *)realloc(s->pts, (size_t)nc * sizeof *np);
        if (!np) return -1;
        s->pts = np;
        s->cap = nc;
    }
    wb_spawn_point *p = &s->pts[s->count];
    memset(p, 0, sizeof *p);
    p->pos[0] = pos[0]; p->pos[1] = pos[1]; p->pos[2] = pos[2];
    p->team = team;
    if (tag) snprintf(p->tag, sizeof p->tag, "%s", tag);
    if (gamemode) snprintf(p->gamemode, sizeof p->gamemode, "%s", gamemode);
    return s->count++;
}

int wb_spawn_count(const wb_spawn_set *s) { return s ? s->count : 0; }

const wb_spawn_point *wb_spawn_get(const wb_spawn_set *s, int index) {
    if (!s || index < 0 || index >= s->count) return NULL;
    return &s->pts[index];
}

static int point_matches(const wb_spawn_point *p, const char *gamemode, int team) {
    if (p->occupied) return 0;
    if (p->team >= 0 && team >= 0 && p->team != team) return 0;
    if (p->gamemode[0] && gamemode && gamemode[0] && strcmp(p->gamemode, gamemode) != 0)
        return 0;
    return 1;
}

int wb_spawn_available(const wb_spawn_set *s, const char *gamemode, int team) {
    if (!s) return 0;
    int n = 0;
    for (int i = 0; i < s->count; i++)
        if (point_matches(&s->pts[i], gamemode, team)) n++;
    return n;
}

int wb_spawn_pick(wb_spawn_set *s, const char *gamemode, int team, float out[3]) {
    if (!s) return -1;
    for (int i = 0; i < s->count; i++) {
        if (!point_matches(&s->pts[i], gamemode, team)) continue;
        if (out) {
            out[0] = s->pts[i].pos[0];
            out[1] = s->pts[i].pos[1];
            out[2] = s->pts[i].pos[2];
        }
        return i;
    }
    return -1;
}

void wb_spawn_occupy(wb_spawn_set *s, int index) {
    if (!s || index < 0 || index >= s->count) return;
    s->pts[index].occupied = 1;
}

void wb_spawn_release(wb_spawn_set *s, int index) {
    if (!s || index < 0 || index >= s->count) return;
    s->pts[index].occupied = 0;
}
