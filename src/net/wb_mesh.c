/*
 * wb_mesh.c -- WuBu Engine server meshing (slice 18). See wb_mesh.h.
 */
#include "wb_mesh.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int      used;
    uint32_t entity;
    float    pos[3];
    int      persistent;
    uint8_t  auth[WB_MESH_MAX_COMPS];
} wb_mesh_ent;

struct wb_mesh_node {
    wb_mesh_ent  *ents;
    int           nents;
    int           cap;

    wb_mesh_zone zones[WB_MESH_MAX_SERVERS];
    int          nzones;
};

wb_mesh_node *wb_mesh_new(void) {
    wb_mesh_node *m = (wb_mesh_node *)calloc(1, sizeof *m);
    if (!m) return NULL;
    m->cap = 256;
    m->ents = (wb_mesh_ent *)calloc((size_t)m->cap, sizeof *m->ents);
    if (!m->ents) { free(m); return NULL; }
    return m;
}

void wb_mesh_free(wb_mesh_node *m) {
    if (!m) return;
    free(m->ents);
    free(m);
}

static wb_mesh_ent *find_ent(wb_mesh_node *m, uint32_t entity) {
    for (int i = 0; i < m->nents; i++)
        if (m->ents[i].used && m->ents[i].entity == entity) return &m->ents[i];
    return NULL;
}

static wb_mesh_ent *slot_ent(wb_mesh_node *m, uint32_t entity) {
    wb_mesh_ent *e = find_ent(m, entity);
    if (e) return e;
    if (m->nents >= m->cap) {
        int nc = m->cap * 2;
        wb_mesh_ent *ne = (wb_mesh_ent *)realloc(m->ents, (size_t)nc * sizeof *ne);
        if (!ne) return NULL;
        m->ents = ne;
        m->cap = nc;
    }
    wb_mesh_ent *slot = &m->ents[m->nents++];
    memset(slot, 0, sizeof *slot);
    slot->used = 1;
    slot->entity = entity;
    for (int c = 0; c < WB_MESH_MAX_COMPS; c++) slot->auth[c] = WB_MESH_CLIENT_AUTH;
    return slot;
}

/* ---- entities ---------------------------------------------------------- */

int wb_mesh_entity_add(wb_mesh_node *m, uint32_t entity, const float pos[3]) {
    if (!m || !pos) return -1;
    if (find_ent(m, entity)) return 0;
    wb_mesh_ent *e = slot_ent(m, entity);
    if (!e) return -1;
    e->pos[0]=pos[0]; e->pos[1]=pos[1]; e->pos[2]=pos[2];
    return 1;
}

void wb_mesh_entity_set_pos(wb_mesh_node *m, uint32_t entity, const float pos[3]) {
    if (!m || !pos) return;
    wb_mesh_ent *e = find_ent(m, entity);
    if (!e) return;
    e->pos[0]=pos[0]; e->pos[1]=pos[1]; e->pos[2]=pos[2];
}

void wb_mesh_entity_remove(wb_mesh_node *m, uint32_t entity) {
    if (!m) return;
    wb_mesh_ent *e = find_ent(m, entity);
    if (e) e->used = 0;
}

int wb_mesh_entity_pos(const wb_mesh_node *m, uint32_t entity, float out[3]) {
    if (!m || !out) return 0;
    for (int i = 0; i < m->nents; i++) {
        if (!m->ents[i].used || m->ents[i].entity != entity) continue;
        out[0]=m->ents[i].pos[0]; out[1]=m->ents[i].pos[1]; out[2]=m->ents[i].pos[2];
        return 1;
    }
    return 0;
}

void wb_mesh_set_persistent(wb_mesh_node *m, uint32_t entity, int persistent) {
    if (!m) return;
    wb_mesh_ent *e = find_ent(m, entity);
    if (e) e->persistent = persistent ? 1 : 0;
}

int wb_mesh_is_persistent(const wb_mesh_node *m, uint32_t entity) {
    if (!m) return 0;
    for (int i = 0; i < m->nents; i++)
        if (m->ents[i].used && m->ents[i].entity == entity) return m->ents[i].persistent;
    return 0;
}

/* ---- zones ------------------------------------------------------------- */

int wb_mesh_zone_add(wb_mesh_node *m, uint32_t server_id,
                     const float center[3], float radius) {
    if (!m || !center || radius <= 0.0f) return -1;
    if (m->nzones >= WB_MESH_MAX_SERVERS) return -1;
    wb_mesh_zone *z = &m->zones[m->nzones++];
    z->id = server_id;
    z->center[0]=center[0]; z->center[1]=center[1]; z->center[2]=center[2];
    z->radius = radius;
    z->active = 1;
    return 0;
}

void wb_mesh_zone_set_active(wb_mesh_node *m, uint32_t server_id, int active) {
    if (!m) return;
    for (int i = 0; i < m->nzones; i++)
        if (m->zones[i].id == server_id) m->zones[i].active = active ? 1 : 0;
}

int wb_mesh_zone_count(const wb_mesh_node *m) { return m ? m->nzones : 0; }

static float dist2(const float a[3], const float b[3]) {
    float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
    return dx*dx + dy*dy + dz*dz;
}

int wb_mesh_zone_for_pos(const wb_mesh_node *m, const float pos[3]) {
    if (!m || !pos) return -1;
    for (int i = 0; i < m->nzones; i++) {
        const wb_mesh_zone *z = &m->zones[i];
        if (!z->active) continue;
        if (dist2(z->center, pos) <= z->radius * z->radius) return (int)z->id;
    }
    return -1;
}

/* ---- authority --------------------------------------------------------- */

void wb_mesh_set_authority(wb_mesh_node *m, uint32_t entity, uint32_t comp,
                           uint8_t server_id) {
    if (!m || comp >= WB_MESH_MAX_COMPS) return;
    wb_mesh_ent *e = slot_ent(m, entity);
    if (e) e->auth[comp] = server_id;
}

uint8_t wb_mesh_authority(const wb_mesh_node *m, uint32_t entity, uint32_t comp) {
    if (!m || comp >= WB_MESH_MAX_COMPS) return WB_MESH_CLIENT_AUTH;
    for (int i = 0; i < m->nents; i++) {
        if (!m->ents[i].used || m->ents[i].entity != entity) continue;
        return m->ents[i].auth[comp];
    }
    return WB_MESH_CLIENT_AUTH;
}

int wb_mesh_entity_authority_all(const wb_mesh_node *m, uint32_t entity,
                                 uint8_t out[WB_MESH_MAX_COMPS]) {
    if (!m || !out) return 0;
    for (int i = 0; i < m->nents; i++) {
        if (!m->ents[i].used || m->ents[i].entity != entity) continue;
        memcpy(out, m->ents[i].auth, WB_MESH_MAX_COMPS);
        return 1;
    }
    return 0;
}

int wb_mesh_handover(wb_mesh_node *m, uint32_t entity, uint8_t from, uint8_t to,
                     wb_mesh_auth_change *out, int max) {
    if (!m) return 0;
    if (from == to) return 0;   /* nothing to move */
    wb_mesh_ent *e = find_ent(m, entity);
    if (!e) return 0;
    int made = 0;
    for (int c = 0; c < WB_MESH_MAX_COMPS; c++) {
        if (e->auth[c] != from) continue;
        e->auth[c] = to;
        if (out && made < max) {
            out[made].entity = entity;
            out[made].comp = (uint32_t)c;
            out[made].authority = to;
        }
        made++;
    }
    return made;
}

int wb_mesh_handover_all(wb_mesh_node *m, uint8_t from, uint8_t to) {
    if (!m) return 0;
    int total = 0;
    for (int i = 0; i < m->nents; i++) {
        if (!m->ents[i].used) continue;
        for (int c = 0; c < WB_MESH_MAX_COMPS; c++)
            if (m->ents[i].auth[c] == from) { m->ents[i].auth[c] = to; total++; }
    }
    return total;
}

int wb_mesh_owner_for_pos(const wb_mesh_node *m, const float pos[3]) {
    return wb_mesh_zone_for_pos(m, pos);
}

/* ---- streaming --------------------------------------------------------- */

static int zone_contains(const wb_mesh_zone *z, const float pos[3]) {
    return dist2(z->center, pos) <= z->radius * z->radius;
}

int wb_mesh_stream_to(const wb_mesh_node *m, uint32_t server_id,
                      uint32_t *out, int max) {
    if (!m || !out || max <= 0) return 0;
    const wb_mesh_zone *z = NULL;
    for (int i = 0; i < m->nzones; i++)
        if (m->zones[i].id == server_id && m->zones[i].active) { z = &m->zones[i]; break; }
    int n = 0;
    for (int i = 0; i < m->nents && n < max; i++) {
        if (!m->ents[i].used) continue;
        int keep = m->ents[i].persistent || (z && zone_contains(z, m->ents[i].pos));
        if (keep) out[n++] = m->ents[i].entity;
    }
    return n;
}

int wb_mesh_evict_from(const wb_mesh_node *m, uint32_t server_id,
                       uint32_t *out, int max) {
    if (!m || !out || max <= 0) return 0;
    const wb_mesh_zone *z = NULL;
    for (int i = 0; i < m->nzones; i++)
        if (m->zones[i].id == server_id && m->zones[i].active) { z = &m->zones[i]; break; }
    int n = 0;
    for (int i = 0; i < m->nents && n < max; i++) {
        if (!m->ents[i].used) continue;
        if (m->ents[i].persistent) continue;                    /* never evicted */
        if (z && zone_contains(z, m->ents[i].pos)) continue;    /* still in zone */
        out[n++] = m->ents[i].entity;
    }
    return n;
}
