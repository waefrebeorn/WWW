#include "wb_tiles.h"
#include <stdlib.h>
#include <string.h>

/* seat = one resident tile (one (qkey,plane) cell). Payload lives inline. */
typedef struct {
    wb_qkey  key;
    uint8_t  plane;
    uint8_t  state;      /* wb_tile_state */
    uint32_t age;        /* LRU tick; higher = older */
    uint8_t  payload[];
} wb_tile_seat;

/* Open addressing: a free cell is NULL. A cell whose seat was removed is a
 * TOMBSTONE (dummy) so probe chains don't break for keys hashed later in the
 * chain; lookups keep probing past tombstones, and inserts may reuse them. */
static wb_tile_seat *const TOMBSTONE = (wb_tile_seat *)1;

struct wb_tile_store {
    size_t      payload_size;
    uint8_t     planes;
    uint32_t    max_resident;
    wb_tile_seat **slots;   /* open-addressing: slot i in [0, cap) */
    uint32_t    cap;        /* power-of-two coverage */
    uint32_t    resident;   /* live seats */
    uint32_t    clock;      /* global age tick */
    uint32_t    tombstones; /* count of tombstoned cells (for reinternal) */
};

/* hash: mix qkey+plane. FNV-ish 32bit over the 64-bit key. */
static uint32_t hash_of(uint64_t key, uint8_t plane, uint32_t cap) {
    uint64_t h = key ^ (plane * 0x9E3779B97F4A7C15ULL);
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return (uint32_t)h & (cap - 1);
}

wb_tile_store *wb_tiles_create(const wb_tile_store_cfg *cfg) {
    uint32_t cap = 1;
    while (cap < cfg->max_resident * 4u) cap <<= 1; /* 25% load factor */
    if (cap < 16) cap = 16;

    wb_tile_store *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->payload_size  = cfg->payload_size;
    s->planes        = cfg->planes ? cfg->planes : 1;
    s->max_resident  = cfg->max_resident;
    s->cap           = cap;
    s->resident      = 0;
    s->clock         = 0;
    s->tombstones    = 0;
    s->slots         = calloc(cap, sizeof(wb_tile_seat *));
    if (!s->slots) { free(s); return NULL; }
    return s;
}

void wb_tiles_destroy(wb_tile_store *s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->cap; i++)
        if (s->slots[i] && s->slots[i] != TOMBSTONE)
            free(s->slots[i]);
    free(s->slots);
    free(s);
}

/* find a live seat index by (key,plane); returns -1 if none. Probing
 * continues past tombstones so chain integrity is preserved. */
static int32_t find_slot(wb_tile_store *s, wb_qkey key, uint8_t plane) {
    uint32_t i = hash_of(key, plane, s->cap);
    for (uint32_t probe = 0; probe < s->cap; probe++) {
        wb_tile_seat *seat = s->slots[i];
        if (!seat) return -1;                 /* empty -> not found */
        if (seat != TOMBSTONE && seat->key == key && seat->plane == plane)
            return (int32_t)i;
        i = (i + 1) & (s->cap - 1);
    }
    return -1;
}

/* linear-probe insert of a live seat. Reuses the FIRST tombstone in the
 * probe chain (preserves chain continuity) if one exists, else the first
 * empty cell. Returns the slot index placed at, or 0 if the table is full. */
static uint32_t insert_slot(wb_tile_store *s, wb_tile_seat *seat) {
    uint32_t si = hash_of(seat->key, seat->plane, s->cap);
    int32_t tomb_spot = -1;
    for (uint32_t probe = 0; probe < s->cap; probe++) {
        wb_tile_seat *cur = s->slots[si];
        if (!cur) {                       /* empty cell */
            uint32_t at = (tomb_spot >= 0) ? (uint32_t)tomb_spot : si;
            s->slots[at] = seat;
            if (tomb_spot >= 0) s->tombstones--;
            return at;
        }
        if (cur == TOMBSTONE && tomb_spot < 0) {
            tomb_spot = (int32_t)si;      /* remember earliest tombstone */
        }
        si = (si + 1) & (s->cap - 1);
    }
    if (tomb_spot >= 0) { s->slots[tomb_spot] = seat; s->tombstones--; return (uint32_t)tomb_spot; }
    return 0;
}

void *wb_tiles_get(wb_tile_store *s, wb_qkey key, uint8_t plane) {
    int32_t i = find_slot(s, key, plane);
    if (i < 0) return NULL;
    wb_tile_seat *seat = s->slots[i];
    if (seat->state != WB_TILE_READY) return NULL;
    seat->age = s->clock++;                 /* touch -> youngest-ish */
    return (void *)seat->payload;
}

/* evict the single oldest READY seat across all planes. Tiles get
 * age = clock++ at insert and on touch, so the OLDEST (LRU) seat is the one
 * with the SMALLEST age value. */
static wb_tile_seat *evict_oldest(wb_tile_store *s) {
    wb_tile_seat *victim = NULL;
    for (uint32_t i = 0; i < s->cap; i++) {
        wb_tile_seat *seat = s->slots[i];
        if (!seat || seat->state != WB_TILE_READY) continue;
        if (!victim || seat->age < victim->age) victim = seat;
    }
    return victim;
}

int wb_tiles_request(wb_tile_store *s, wb_qkey key, uint8_t plane,
                     wb_tile_load_fn loader, void *user) {
    int32_t i = find_slot(s, key, plane);
    if (i >= 0) {
        wb_tile_seat *seat = s->slots[i];
        if (seat->state == WB_TILE_READY) {
            seat->age = s->clock++;
            return 1;
        }
        return 0;   /* ERROR state: caller may retry later */
    }

    /* evict until under the seat budget before inserting */
    if (s->resident >= s->max_resident) {
        wb_tile_seat *victim = evict_oldest(s);
        if (!victim) return 0;   /* all empty/error, can't make room */
        /* tombstone the victim's slot (NOT NULL, to keep chains intact) */
        int32_t vi = find_slot(s, victim->key, victim->plane);
        free(victim);
        if (vi >= 0) { s->slots[vi] = TOMBSTONE; s->tombstones++; }
        s->resident--;
    }

    wb_tile_seat *seat = calloc(1, sizeof(wb_tile_seat) + s->payload_size);
    if (!seat) return 0;
    seat->key = key; seat->plane = plane; seat->state = WB_TILE_EMPTY;
    seat->age = s->clock++;

    int rc = loader(user, key, plane, seat->payload, s->payload_size);
    seat->state = (rc == 0) ? WB_TILE_READY : WB_TILE_ERROR;

    insert_slot(s, seat);
    if (seat->state == WB_TILE_READY) s->resident++;

    return (seat->state == WB_TILE_READY);
}

void wb_tiles_evict(wb_tile_store *s, wb_qkey key, uint8_t plane) {
    int32_t i = find_slot(s, key, plane);
    if (i < 0) return;
    wb_tile_seat *seat = s->slots[i];
    if (seat->state == WB_TILE_READY) s->resident--;
    free(seat);
    s->slots[i] = TOMBSTONE;        /* tombstone, not NULL */
    s->tombstones++;
}

uint32_t wb_tiles_resident(const wb_tile_store *s) { return s->resident; }
