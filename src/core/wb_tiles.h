/*
 * wb_tiles.h -- Quadkey-addressed tile store for WuBu Engine.
 *
 * The resident-world layer on top of the spatial core. Turns 1:1-Earth
 * coordinates into loadable land: request a tile by quadkey, keep a bounded
 * resident set in RAM (the streamed world), evict by age (the ambient
 * culling LRU). A tile is the payload world: terrain or content bytes per
 * plane. Paging is explicit; nothing is resident until requested.
 */
#ifndef WB_TILES_H
#define WB_TILES_H

#include <stdint.h>
#include <stddef.h>
#include "wb_spatial.h"

typedef struct wb_tile_store wb_tile_store;

typedef struct {
    size_t   payload_size;   /* bytes per tile payload (per plane) */
    uint32_t max_resident;   /* tiles resident in RAM (sum across planes) */
    uint8_t  planes;         /* number of parallel planes (1=terrain) */
} wb_tile_store_cfg;

typedef enum {
    WB_TILE_EMPTY = 0,
    WB_TILE_READY = 1,   /* resident, payload filled */
    WB_TILE_ERROR = 2,   /* last load failed */
} wb_tile_state;

/* Pull a tile's bytes from the geodata source into *out (payload_size bytes).
 * Sync engine fills here and the store marks READY; 0=ok, nonzero=error. */
typedef int (*wb_tile_load_fn)(void *user, wb_qkey key, uint8_t plane,
                               void *out, size_t out_len);

wb_tile_store *wb_tiles_create(const wb_tile_store_cfg *cfg);
void           wb_tiles_destroy(wb_tile_store *s);

/* Resident payload of a tile, or NULL if not resident/ready. */
void *wb_tiles_get(wb_tile_store *s, wb_qkey key, uint8_t plane);

/* Demand a tile: load now if not resident, evicting the oldest if over the
 * seat limit. Returns 1 if the tile is resident (READY) after this call,
 * 0 if it errored or was rejected. */
int wb_tiles_request(wb_tile_store *s, wb_qkey key, uint8_t plane,
                     wb_tile_load_fn loader, void *user);

void     wb_tiles_evict(wb_tile_store *s, wb_qkey key, uint8_t plane);
uint32_t wb_tiles_resident(const wb_tile_store *s);

#endif /* WB_TILES_H */
