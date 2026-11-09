/*
 * wb_net_server.c -- the authoritative server: entity simulation, AOI
 * snapshot building and delta compression. See wb_net_server.h.
 */
#include "wb_net_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

wb_net_server *wb_net_server_new(float tick_rate) {
    wb_net_server *s = (wb_net_server *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->tick_rate = tick_rate > 0.0f ? tick_rate : 20.0f;
    s->aoi_radius = 500.0f;
    s->next_id = 1;              /* 0 is the invalid sentinel */
    return s;
}

void wb_net_server_free(wb_net_server *s) { free(s); }

static wb_net_ent *find_by_client(wb_net_server *s, int client_id) {
    for (int i = 0; i < WB_NET_MAX_ENTITIES; i++)
        if (s->ents[i].used && s->ents[i].client_id == client_id) return &s->ents[i];
    return NULL;
}

static wb_net_ent *find_by_id(wb_net_server *s, uint32_t id) {
    for (int i = 0; i < WB_NET_MAX_ENTITIES; i++)
        if (s->ents[i].used && s->ents[i].id == id) return &s->ents[i];
    return NULL;
}

uint32_t wb_net_server_add_entity(wb_net_server *s, const double lonlat[2],
                                  const float pos[3]) {
    for (int i = 0; i < WB_NET_MAX_ENTITIES; i++) {
        if (s->ents[i].used) continue;
        wb_net_ent *e = &s->ents[i];
        memset(e, 0, sizeof *e);
        e->used = 1;
        e->id = s->next_id++;
        e->client_id = -1;
        e->lonlat[0] = lonlat[0];
        e->lonlat[1] = lonlat[1];
        if (pos) { e->pos[0]=pos[0]; e->pos[1]=pos[1]; e->pos[2]=pos[2]; }
        return e->id;
    }
    return 0;
}

uint32_t wb_net_server_add_player(wb_net_server *s, int client_id,
                                  const double lonlat[2], const float pos[3]) {
    uint32_t id = wb_net_server_add_entity(s, lonlat, pos);
    wb_net_ent *e = find_by_id(s, id);
    if (e) e->client_id = client_id;
    return id;
}

void wb_net_server_set_aoi_radius(wb_net_server *s, float radius_m) {
    s->aoi_radius = radius_m > 0.0f ? radius_m : 1.0f;
}

void wb_net_server_apply_impulse(wb_net_server *s, uint32_t id, float dx, float dy) {
    wb_net_ent *e = find_by_id(s, id);
    if (!e) return;
    e->pos[0] += dx;
    e->pos[1] += dy;
}

void wb_net_server_remove_entity(wb_net_server *s, uint32_t id) {
    wb_net_ent *e = find_by_id(s, id);
    if (e) e->used = 0;
}

wb_net_ent_state wb_net_server_entity(wb_net_server *s, uint32_t id) {
    wb_net_ent_state st;
    memset(&st, 0, sizeof st);
    wb_net_ent *e = find_by_id(s, id);
    if (!e) return st;
    st.id = e->id;
    st.pos[0]=e->pos[0]; st.pos[1]=e->pos[1]; st.pos[2]=e->pos[2];
    st.vel[0]=e->vel[0]; st.vel[1]=e->vel[1]; st.vel[2]=e->vel[2];
    st.yaw = e->yaw;
    return st;
}

void wb_net_server_recv_input(wb_net_server *s, int client_id, const wb_net_input *in) {
    wb_net_ent *e = find_by_client(s, client_id);
    if (!e) return;
    e->move_x = in->move_x;
    e->move_y = in->move_y;
    e->want_yaw = in->yaw;
    if (in->seq > e->ack_seq) e->ack_seq = in->seq;
}

void wb_net_server_tick(wb_net_server *s) {
    float dt = 1.0f / s->tick_rate;
    for (int i = 0; i < WB_NET_MAX_ENTITIES; i++) {
        wb_net_ent *e = &s->ents[i];
        if (!e->used) continue;
        e->vel[0] = e->move_x * WB_NET_SPEED;
        e->vel[1] = e->move_y * WB_NET_SPEED;
        e->pos[0] += e->vel[0] * dt;
        e->pos[1] += e->vel[1] * dt;
        if (e->client_id >= 0) e->yaw = e->want_yaw;
    }
    s->tick++;
}

void wb_net_server_snapshot(wb_net_server *s, int client_id, wb_net_snapshot *out) {
    memset(out, 0, sizeof *out);
    out->tick = s->tick;
    wb_net_ent *me = find_by_client(s, client_id);
    if (!me) return;
    out->ack_seq = me->ack_seq;

    wb_net_last *last = s->last[client_id];

    /* the client's own entity is always authoritative, always sent */
    out->ents[out->count].id = me->id;
    memcpy(out->ents[out->count].pos, me->pos, sizeof me->pos);
    memcpy(out->ents[out->count].vel, me->vel, sizeof me->vel);
    out->ents[out->count].yaw = me->yaw;
    out->count++;
    for (int i = 0; i < WB_NET_MAX_ENTITIES; i++) {
        if (last[i].id == me->id) {
            last[i].valid = 1;
            memcpy(last[i].pos, me->pos, sizeof me->pos);
            last[i].yaw = me->yaw;
        }
    }

    for (int i = 0; i < WB_NET_MAX_ENTITIES; i++) {
        wb_net_ent *e = &s->ents[i];
        if (!e->used || e == me) continue;
        if (out->count >= WB_NET_MAX_ENTITIES) break;
        if (!wb_net_aoi_visible(me->lonlat, e->lonlat, s->aoi_radius)) continue;

        /* delta: skip anything that has not moved since the last snapshot */
        wb_net_last *l = &last[i];
        if (l->valid && l->id == e->id) {
            float dx = e->pos[0] - l->pos[0];
            float dy = e->pos[1] - l->pos[1];
            float dyaw = e->yaw - l->yaw;
            if (dx*dx + dy*dy < WB_NET_DELTA_EPS * WB_NET_DELTA_EPS &&
                fabsf(dyaw) < WB_NET_DELTA_EPS) continue;
        }

        out->ents[out->count].id = e->id;
        memcpy(out->ents[out->count].pos, e->pos, sizeof e->pos);
        memcpy(out->ents[out->count].vel, e->vel, sizeof e->vel);
        out->ents[out->count].yaw = e->yaw;
        out->count++;

        l->valid = 1;
        l->id = e->id;
        memcpy(l->pos, e->pos, sizeof e->pos);
        l->yaw = e->yaw;
    }
}

uint32_t wb_net_server_tick_count(const wb_net_server *s) { return s->tick; }

int wb_net_server_entity_count(const wb_net_server *s) {
    int n = 0;
    for (int i = 0; i < WB_NET_MAX_ENTITIES; i++) if (s->ents[i].used) n++;
    return n;
}
