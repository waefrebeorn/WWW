/*
 * wb_dist.h -- WuBu Engine experience distribution (slice 16).
 *
 * The "world space and distribution system": experiences are spawned into the
 * world as instances, players discover them through a directory, matchmaking
 * picks one, and portals in world space carry them in -- the Yu-Gi-Oh stadium
 * mechanic.
 *
 * C11, no external deps.
 */
#ifndef WB_DIST_H
#define WB_DIST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_DIST_MAX_PLAYERS   128
#define WB_DIST_MAX_INSTANCES 256
#define WB_DIST_ID_LEN        64

typedef enum {
    WB_DIST_WAITING = 0,   /* open, not started */
    WB_DIST_PLAYING,       /* running */
    WB_DIST_ENDED          /* finished; will be reaped */
} wb_dist_state;

typedef struct wb_dist_instance wb_dist_instance;

typedef struct {
    char  id[WB_DIST_ID_LEN];
    char  gamemode[256];
    char  region[32];
    int   capacity;
    int   players;
    int   state;           /* wb_dist_state */
    int   ping_ms;
} wb_dist_info;

/* ---- instances --------------------------------------------------------- */

wb_dist_instance *wb_dist_instance_new(const char *gamemode, int capacity,
                                       const char *region);
void  wb_dist_instance_free(wb_dist_instance *in);

const char *wb_dist_instance_id(const wb_dist_instance *in);
int   wb_dist_instance_join(wb_dist_instance *in, const char *player_id);
int   wb_dist_instance_leave(wb_dist_instance *in, const char *player_id);
int   wb_dist_instance_player_count(const wb_dist_instance *in);
int   wb_dist_instance_has_player(const wb_dist_instance *in, const char *player_id);
int   wb_dist_instance_capacity(const wb_dist_instance *in);
int   wb_dist_instance_is_full(const wb_dist_instance *in);

void  wb_dist_instance_set_state(wb_dist_instance *in, int state);
int   wb_dist_instance_state(const wb_dist_instance *in);
void  wb_dist_instance_set_ping(wb_dist_instance *in, int ping_ms);
void  wb_dist_instance_info(const wb_dist_instance *in, wb_dist_info *out);

/* ---- registry / discovery --------------------------------------------- */

typedef struct wb_dist_registry wb_dist_registry;

wb_dist_registry *wb_dist_registry_new(void);
void  wb_dist_registry_free(wb_dist_registry *r);

void  wb_dist_registry_add(wb_dist_registry *r, wb_dist_instance *in);
void  wb_dist_registry_remove(wb_dist_registry *r, const char *id);
int   wb_dist_registry_count(const wb_dist_registry *r);
wb_dist_instance *wb_dist_registry_find(wb_dist_registry *r, const char *id);

/* Snapshot the directory (does not transfer ownership). */
int   wb_dist_registry_list(const wb_dist_registry *r, wb_dist_info *out, int max);

/* ---- matchmaking ------------------------------------------------------- */

/* Pick the best instance for a player: a waiting instance with room first
 * (least populated), then a playing one with room, then the lowest ping.
 * Returns NULL when nothing suitable exists. */
wb_dist_instance *wb_dist_match(wb_dist_registry *r, const char *gamemode);

/* Join the best instance for `player_id` in one call. Returns the instance or
 * NULL. `out_id` (if not NULL, WB_DIST_ID_LEN bytes) receives the id. */
wb_dist_instance *wb_dist_join_best(wb_dist_registry *r, const char *gamemode,
                                    const char *player_id, char *out_id);

/* ---- portals ----------------------------------------------------------- */

typedef struct {
    float pos[3];
    float radius;
    char  target[WB_DIST_ID_LEN];   /* experience id, or a gamemode name */
    int   active;
} wb_portal;

void wb_portal_init(wb_portal *p);
int  wb_portal_check(const wb_portal *p, const float player_pos[3]);

#ifdef __cplusplus
}
#endif

#endif /* WB_DIST_H */
