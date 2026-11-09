/*
 * wb_dist.c -- WuBu Engine experience distribution (slice 16). See wb_dist.h.
 */
#include "wb_dist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================================================================== */
/* instances                                                             */
/* ==================================================================== */

struct wb_dist_instance {
    char  id[WB_DIST_ID_LEN];
    char  gamemode[256];
    char  region[32];
    int   capacity;
    int   state;
    int   ping_ms;
    int   nplayers;
    char  players[WB_DIST_MAX_PLAYERS][WB_DIST_ID_LEN];
};

static uint32_t next_instance_id(void) {
    static uint32_t counter = 0;
    return ++counter;
}

wb_dist_instance *wb_dist_instance_new(const char *gamemode, int capacity,
                                       const char *region) {
    wb_dist_instance *in = (wb_dist_instance *)calloc(1, sizeof *in);
    if (!in) return NULL;
    snprintf(in->id, sizeof in->id, "exp-%u", next_instance_id());
    snprintf(in->gamemode, sizeof in->gamemode, "%s", gamemode ? gamemode : "unknown");
    snprintf(in->region, sizeof in->region, "%s", region ? region : "us-east");
    in->capacity = capacity > 0 ? capacity : 100;
    in->state = WB_DIST_WAITING;
    in->ping_ms = 0;
    return in;
}

void wb_dist_instance_free(wb_dist_instance *in) { free(in); }

const char *wb_dist_instance_id(const wb_dist_instance *in) { return in->id; }
int wb_dist_instance_player_count(const wb_dist_instance *in) { return in->nplayers; }
int wb_dist_instance_capacity(const wb_dist_instance *in) { return in->capacity; }
int wb_dist_instance_is_full(const wb_dist_instance *in) { return in->nplayers >= in->capacity; }
int wb_dist_instance_state(const wb_dist_instance *in) { return in->state; }
void wb_dist_instance_set_state(wb_dist_instance *in, int state) { in->state = state; }
void wb_dist_instance_set_ping(wb_dist_instance *in, int ping_ms) { in->ping_ms = ping_ms; }

int wb_dist_instance_has_player(const wb_dist_instance *in, const char *player_id) {
    for (int i = 0; i < in->nplayers; i++)
        if (!strcmp(in->players[i], player_id)) return 1;
    return 0;
}

int wb_dist_instance_join(wb_dist_instance *in, const char *player_id) {
    if (!in || !player_id || !player_id[0]) return -1;
    /* Idempotent first: a player already in must not be refused just because
     * the instance happens to be full. */
    if (wb_dist_instance_has_player(in, player_id)) return 0;
    if (in->nplayers >= in->capacity) return -1;
    snprintf(in->players[in->nplayers++], WB_DIST_ID_LEN, "%s", player_id);
    return 1;
}

int wb_dist_instance_leave(wb_dist_instance *in, const char *player_id) {
    if (!in || !player_id) return -1;
    for (int i = 0; i < in->nplayers; i++) {
        if (strcmp(in->players[i], player_id)) continue;
        /* swap-remove */
        if (i != in->nplayers - 1)
            memcpy(in->players[i], in->players[in->nplayers - 1], WB_DIST_ID_LEN);
        in->nplayers--;
        return 1;
    }
    return 0;
}

void wb_dist_instance_info(const wb_dist_instance *in, wb_dist_info *out) {
    memset(out, 0, sizeof *out);
    snprintf(out->id, sizeof out->id, "%s", in->id);
    snprintf(out->gamemode, sizeof out->gamemode, "%s", in->gamemode);
    snprintf(out->region, sizeof out->region, "%s", in->region);
    out->capacity = in->capacity;
    out->players = in->nplayers;
    out->state = in->state;
    out->ping_ms = in->ping_ms;
}

/* ==================================================================== */
/* registry                                                               */
/* ==================================================================== */

struct wb_dist_registry {
    wb_dist_instance *instances[WB_DIST_MAX_INSTANCES];
    int count;
};

wb_dist_registry *wb_dist_registry_new(void) {
    return (wb_dist_registry *)calloc(1, sizeof(wb_dist_registry));
}

void wb_dist_registry_free(wb_dist_registry *r) {
    if (!r) return;
    for (int i = 0; i < r->count; i++) wb_dist_instance_free(r->instances[i]);
    free(r);
}

void wb_dist_registry_add(wb_dist_registry *r, wb_dist_instance *in) {
    if (!r || !in || r->count >= WB_DIST_MAX_INSTANCES) return;
    r->instances[r->count++] = in;
}

void wb_dist_registry_remove(wb_dist_registry *r, const char *id) {
    if (!r || !id) return;
    for (int i = 0; i < r->count; i++) {
        if (strcmp(r->instances[i]->id, id)) continue;
        wb_dist_instance_free(r->instances[i]);
        if (i != r->count - 1) r->instances[i] = r->instances[r->count - 1];
        r->count--;
        return;
    }
}

/* Remove `player_id` from EVERY instance (a portal travel detaches them from
 * whichever world they were in). Returns how many instances they left. */
int wb_dist_registry_leave_player(wb_dist_registry *r, const char *player_id) {
    if (!r || !player_id) return 0;
    int left = 0;
    for (int i = 0; i < r->count; i++) {
        if (wb_dist_instance_leave(r->instances[i], player_id) == 1) left++;
        if (r->instances[i]->nplayers <= 0) {          /* empty world is reaped */
            wb_dist_instance_free(r->instances[i]);
            if (i != r->count - 1) r->instances[i] = r->instances[r->count - 1];
            r->count--; i--;
        }
    }
    return left;
}

int wb_dist_registry_count(const wb_dist_registry *r) { return r ? r->count : 0; }

wb_dist_instance *wb_dist_registry_find(wb_dist_registry *r, const char *id) {
    if (!r || !id) return NULL;
    for (int i = 0; i < r->count; i++)
        if (!strcmp(r->instances[i]->id, id)) return r->instances[i];
    return NULL;
}

int wb_dist_registry_list(const wb_dist_registry *r, wb_dist_info *out, int max) {
    if (!r || !out) return 0;
    int n = r->count < max ? r->count : max;
    for (int i = 0; i < n; i++) wb_dist_instance_info(r->instances[i], &out[i]);
    return n;
}

/* ==================================================================== */
/* matchmaking                                                            */
/* ==================================================================== */

static int instance_score(const wb_dist_instance *in, const char *gamemode) {
    if (strcmp(in->gamemode, gamemode) != 0) return -1;   /* wrong game */
    if (wb_dist_instance_is_full(in)) return -1;          /* no room */
    if (in->state == WB_DIST_ENDED) return -1;
    /* Prefer waiting over playing, then the least populated, then low ping. */
    int score = 1000;
    if (in->state == WB_DIST_WAITING) score += 500;
    score -= in->nplayers;
    score -= in->ping_ms / 10;
    return score;
}

wb_dist_instance *wb_dist_match(wb_dist_registry *r, const char *gamemode) {
    if (!r || !gamemode) return NULL;
    wb_dist_instance *best = NULL;
    int best_score = -1;
    for (int i = 0; i < r->count; i++) {
        int s = instance_score(r->instances[i], gamemode);
        if (s > best_score) { best_score = s; best = r->instances[i]; }
    }
    return best;
}

wb_dist_instance *wb_dist_join_best(wb_dist_registry *r, const char *gamemode,
                                    const char *player_id, char *out_id) {
    wb_dist_instance *in = wb_dist_match(r, gamemode);
    if (!in) return NULL;
    if (wb_dist_instance_join(in, player_id) < 0) return NULL;
    if (out_id) snprintf(out_id, WB_DIST_ID_LEN, "%s", in->id);
    return in;
}

/* ==================================================================== */
/* portals                                                                */
/* ==================================================================== */

void wb_portal_init(wb_portal *p) {
    memset(p, 0, sizeof *p);
    p->radius = 2.0f;
    p->active = 1;
}

int wb_portal_check(const wb_portal *p, const float player_pos[3]) {
    if (!p || !p->active) return 0;
    float dx = player_pos[0] - p->pos[0];
    float dy = player_pos[1] - p->pos[1];
    float dz = player_pos[2] - p->pos[2];
    return (dx*dx + dy*dy + dz*dz) <= (p->radius * p->radius);
}
