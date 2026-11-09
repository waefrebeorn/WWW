/*
 * wb_mesh.h -- WuBu Engine server meshing (slice 18).
 *
 * A world this size is served by many servers, so authority has to move:
 * each entity's components are owned by exactly one server at a time, a
 * handover transfers that ownership when an entity crosses a zone boundary,
 * and streaming decides which entities each server keeps resident (AOI plus
 * the persistent set).
 *
 * C11, no external deps.
 */
#ifndef WB_MESH_H
#define WB_MESH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_MESH_MAX_SERVERS  64
#define WB_MESH_MAX_ENTITIES 4096
#define WB_MESH_MAX_COMPS    8
#define WB_MESH_CLIENT_AUTH  0xFF   /* owned by the client, not any server */

typedef struct wb_mesh_node wb_mesh_node;

typedef struct {
    uint32_t id;                 /* server id */
    float    center[3];          /* zone center in world space */
    float    radius;             /* zone radius */
    int      active;
} wb_mesh_zone;

typedef struct {
    uint32_t entity;
    uint32_t comp;
    uint8_t  authority;          /* server id, or WB_MESH_CLIENT_AUTH */
} wb_mesh_auth_change;

wb_mesh_node *wb_mesh_new(void);
void  wb_mesh_free(wb_mesh_node *m);

/* ---- entities ---------------------------------------------------------- */

int   wb_mesh_entity_add(wb_mesh_node *m, uint32_t entity, const float pos[3]);
void  wb_mesh_entity_set_pos(wb_mesh_node *m, uint32_t entity, const float pos[3]);
void  wb_mesh_entity_remove(wb_mesh_node *m, uint32_t entity);
int   wb_mesh_entity_pos(const wb_mesh_node *m, uint32_t entity, float out[3]);

void  wb_mesh_set_persistent(wb_mesh_node *m, uint32_t entity, int persistent);
int   wb_mesh_is_persistent(const wb_mesh_node *m, uint32_t entity);

/* ---- zones ------------------------------------------------------------- */

int   wb_mesh_zone_add(wb_mesh_node *m, uint32_t server_id,
                       const float center[3], float radius);
void  wb_mesh_zone_set_active(wb_mesh_node *m, uint32_t server_id, int active);
int   wb_mesh_zone_count(const wb_mesh_node *m);
int   wb_mesh_zone_for_pos(const wb_mesh_node *m, const float pos[3]);

/* ---- authority --------------------------------------------------------- */

void  wb_mesh_set_authority(wb_mesh_node *m, uint32_t entity, uint32_t comp,
                            uint8_t server_id);
uint8_t wb_mesh_authority(const wb_mesh_node *m, uint32_t entity, uint32_t comp);
int   wb_mesh_entity_authority_all(const wb_mesh_node *m, uint32_t entity,
                                   uint8_t out[WB_MESH_MAX_COMPS]);

/* Transfer authority of an entity's components from one server to another.
 * `out` (if not NULL, capacity `max`) receives the changes actually made. */
int   wb_mesh_handover(wb_mesh_node *m, uint32_t entity, uint8_t from, uint8_t to,
                       wb_mesh_auth_change *out, int max);

/* Transfer every entity `from` owns to `to`. */
int   wb_mesh_handover_all(wb_mesh_node *m, uint8_t from, uint8_t to);

/* The server that should own an entity at `pos` (its zone), or -1. */
int   wb_mesh_owner_for_pos(const wb_mesh_node *m, const float pos[3]);

/* ---- streaming --------------------------------------------------------- */

/* Entities a server must keep resident: everything in its zone plus the
 * persistent set. Returns the count written to `out`. */
int   wb_mesh_stream_to(const wb_mesh_node *m, uint32_t server_id,
                        uint32_t *out, int max);

/* Entities that are no longer in a server's zone and are not persistent --
 * the ones it should evict. */
int   wb_mesh_evict_from(const wb_mesh_node *m, uint32_t server_id,
                         uint32_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* WB_MESH_H */
