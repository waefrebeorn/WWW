/*
 * wb_layer.h -- per-level capacity and layering (LP-106 stone 1).
 *
 * Second Life caps a region at 100 avatars; WoW shards crowded zones into layers
 * to keep parties together. Both are the same mechanism: a PLACE has a capacity,
 * and when it is exceeded the system opens a SIBLING instance of the same place
 * rather than turning players away.
 *
 * Two rules make it work:
 *
 *   CO-LOCATION. A party is a unit. Its members are always on the same layer, and
 *   a party that rejoins a place returns to the layer it was on rather than being
 *   spread across siblings.
 *
 *   A PACK POLICY. WB_LAYER_PACK fills the fullest layer that still has room
 *   (dense layers, few of them -- WoW's behaviour). WB_LAYER_SPREAD puts the party
 *   on the emptiest layer (even load -- a hot stadium opening siblings). Both
 *   break ties to the LOWEST layer id, so two servers agree without consulting.
 *
 * A layer is never created past the pool's ceiling, and a party larger than a
 * whole layer is refused rather than split.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_LAYER_H
#define WB_LAYER_H

#include <stdint.h>

#define WB_LAYER_MAX_PARTIES 32
#define WB_LAYER_NAME 32

typedef enum {
    WB_LAYER_PACK = 0,     /* fill the fullest layer that has room */
    WB_LAYER_SPREAD = 1    /* fill the emptiest layer that has room */
} wb_layer_mode;

typedef struct {
    uint64_t id;
    uint32_t size;
} wb_layer_party;

typedef struct {
    uint64_t id;
    uint32_t capacity;
    uint32_t used;
    wb_layer_party parties[WB_LAYER_MAX_PARTIES];
    uint32_t n;
} wb_layer_inst;

typedef struct {
    wb_layer_inst *layers;
    uint32_t cap;
    uint32_t n;
    uint32_t max_layers;        /* the spawn ceiling */
    uint32_t layer_capacity;    /* every layer's capacity */
    uint32_t mode;              /* a wb_layer_mode */
    uint64_t next_layer_id;
} wb_layer_pool;

typedef enum {
    WB_LAYER_OK = 0,
    WB_LAYER_REFUSED_TOO_BIG,   /* the party cannot fit a whole layer */
    WB_LAYER_REFUSED_CEILING    /* the layer ceiling is reached and none has room */
} wb_layer_reason;

typedef struct {
    uint64_t layer;
    uint32_t reason;
} wb_layer_result;

int  wb_layer_init(wb_layer_pool *p, uint32_t cap, uint32_t max_layers,
                   uint32_t layer_capacity, uint32_t mode);
void wb_layer_free(wb_layer_pool *p);
void wb_layer_clear(wb_layer_pool *p);

/* Open a sibling layer. Returns 0 on success, -1 at the ceiling or when the pool
 * is full. */
int  wb_layer_spawn(wb_layer_pool *p);

wb_layer_result wb_layer_join(wb_layer_pool *p, uint64_t party, uint32_t size);

/* Remove a party from whatever layer holds it. Returns 0 on success, -1 if the
 * party is on no layer. */
int  wb_layer_leave(wb_layer_pool *p, uint64_t party);

uint32_t wb_layer_count(const wb_layer_pool *p);
uint32_t wb_layer_used(const wb_layer_pool *p, uint64_t layer);
uint32_t wb_layer_free_space(const wb_layer_pool *p, uint64_t layer);
int      wb_layer_find(const wb_layer_pool *p, uint64_t party, uint64_t *layer);
int      wb_layer_contains(const wb_layer_pool *p, uint64_t layer, uint64_t party);

#endif /* WB_LAYER_H */
