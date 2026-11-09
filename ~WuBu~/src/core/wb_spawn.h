/*
 * wb_spawn.h -- WuBu Engine spawn points.
 *
 * A gametype declares spawn points in the world: where a player appears when
 * they spawn into a game space. Points are tagged by team and by gametype, so
 * "red base" and "blue base" are data, not code.
 *
 * C11, no external deps.
 */
#ifndef WB_SPAWN_H
#define WB_SPAWN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_SPAWN_MAX_POINTS 512
#define WB_SPAWN_TAG_LEN    64
#define WB_SPAWN_MODE_LEN   128

typedef struct {
    float pos[3];
    int   team;                          /* -1 = any team */
    char  tag[WB_SPAWN_TAG_LEN];
    char  gamemode[WB_SPAWN_MODE_LEN];   /* empty = every gametype */
    int   occupied;                      /* runtime: someone is standing here */
} wb_spawn_point;

typedef struct wb_spawn_set wb_spawn_set;

wb_spawn_set *wb_spawn_new(void);
void wb_spawn_free(wb_spawn_set *s);

/* Add a spawn point. Returns its index (>=0) or -1 when the set is full. */
int wb_spawn_add(wb_spawn_set *s, const float pos[3], int team,
                 const char *tag, const char *gamemode);

int wb_spawn_count(const wb_spawn_set *s);
const wb_spawn_point *wb_spawn_get(const wb_spawn_set *s, int index);

/* Pick a point for (gamemode, team): prefer an unoccupied point whose team
 * matches (or is -1) and whose gamemode matches (or is empty). Ties break to
 * the lowest index, which is deterministic. Copies the position into `out`.
 * Returns the index, or -1 when nothing is suitable. */
int wb_spawn_pick(wb_spawn_set *s, const char *gamemode, int team, float out[3]);

/* wb_spawn_pick that prefers a point carrying `tag` (a portal's landing zone),
 * falling back to any matching point. tag NULL/empty == wb_spawn_pick. */
int wb_spawn_pick_tag(wb_spawn_set *s, const char *gamemode, int team,
                      const char *tag, float out[3]);

/* Reserve / release a picked point, so two players do not stack. */
void wb_spawn_occupy(wb_spawn_set *s, int index);
void wb_spawn_release(wb_spawn_set *s, int index);

/* Number of points matching a (gamemode, team) query. */
int wb_spawn_available(const wb_spawn_set *s, const char *gamemode, int team);

#ifdef __cplusplus
}
#endif

#endif /* WB_SPAWN_H */
