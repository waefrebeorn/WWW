/*
 * wb_net.c -- WuBu Engine networking core (slice 13). See wb_net.h.
 *
 * Server-authoritative: the server simulates, clients predict their own
 * entity and reconcile. Interest management runs on the engine's quadkey
 * spatial core so bandwidth scales with what is nearby, not with the world.
 */
#include "wb_net.h"
#include "../core/wb_spatial.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WB_NET_SPEED 5.0f      /* m/s, matches the character controller walk */
#define WB_NET_EPS   1e-4f

/* ==================================================================== */
/* AOI on the quadkey                                                    */
/* ==================================================================== */

#define WB_NET_EARTH_CIRCUMFERENCE 40075016.686

int wb_net_aoi_level(float radius_m) {
    if (radius_m <= 0.0f) return 0;
    int level = 0;
    double cell = WB_NET_EARTH_CIRCUMFERENCE;
    while (cell > (double)radius_m && level < (int)WB_MAX_LEVEL) {
        cell *= 0.5;
        level++;
    }
    return level;
}

int wb_net_aoi_visible(const double origin_lonlat[2], const double pos_lonlat[2],
                       float radius_m) {
    int level = wb_net_aoi_level(radius_m);
    wb_qkey a = wb_qkey_from_lonlat(origin_lonlat[0], origin_lonlat[1], (uint32_t)level);
    wb_qkey b = wb_qkey_from_lonlat(pos_lonlat[0], pos_lonlat[1], (uint32_t)level);
    int dtx = (int)wb_qkey_tx(a) - (int)wb_qkey_tx(b);
    int dty = (int)wb_qkey_ty(a) - (int)wb_qkey_ty(b);
    if (dtx < 0) dtx = -dtx;
    if (dty < 0) dty = -dty;
    /* same cell or an immediately adjacent one */
    return dtx <= 1 && dty <= 1;
}

/* ==================================================================== */
/* server                                                               */
/* ==================================================================== */

typedef struct {
    uint32_t id;
    int      client_id;      /* -1 for non-player entities */
    double   lonlat[2];
    float    pos[3];
    float    vel[3];
    float    yaw;
    float    move_x, move_y; /* last input's desired velocity */
    float    want_yaw;
    int      used;
    uint32_t ack_seq;
} wb_net_ent;

/* Per-client record of what was last replicated, for delta compression. */
typedef struct {
    int      valid;
    uint32_t id;
    float    pos[3];
    float    yaw;
} wb_net_last;

#define WB_NET_DELTA_EPS 0.001f

struct wb_net_server {
    wb_net_ent ents[WB_NET_MAX_ENTITIES];
    uint32_t   next_id;
    uint32_t   tick;
    float      tick_rate;
    float      aoi_radius;
    wb_net_last last[WB_NET_MAX_CLIENTS][WB_NET_MAX_ENTITIES];
};

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
        if (last[i].id == me->id) { last[i].valid = 1; memcpy(last[i].pos, me->pos, sizeof me->pos); last[i].yaw = me->yaw; }
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

/* ==================================================================== */
/* client                                                               */
/* ==================================================================== */

typedef struct {
    uint32_t seq;
    wb_net_input in;
    wb_net_ent_state after;   /* predicted state right after this input */
} wb_net_record;

struct wb_net_client {
    uint32_t entity;
    float    aoi_radius;
    uint32_t next_seq;
    wb_net_ent_state state;
    wb_net_record ring[WB_NET_MAX_INPUTS];
    int      head, count;      /* circular buffer */
    int      corrections;
    float    last_error;
    wb_net_send_fn send_fn;
    void          *send_ud;
};

void wb_net_client_set_send_callback(wb_net_client *c, wb_net_send_fn fn, void *ud) {
    c->send_fn = fn;
    c->send_ud = ud;
}

wb_net_client *wb_net_client_new(uint32_t entity, float aoi_radius) {
    wb_net_client *c = (wb_net_client *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->entity = entity;
    c->aoi_radius = aoi_radius;
    return c;
}

void wb_net_client_free(wb_net_client *c) { free(c); }

static void apply_move(wb_net_ent_state *st, const wb_net_input *in, float dt) {
    st->vel[0] = in->move_x * WB_NET_SPEED;
    st->vel[1] = in->move_y * WB_NET_SPEED;
    st->pos[0] += st->vel[0] * dt;
    st->pos[1] += st->vel[1] * dt;
    st->yaw = in->yaw;
}

void wb_net_client_apply_input(wb_net_client *c, const wb_net_input *in, float dt) {
    wb_net_input i = *in;
    i.seq = c->next_seq++;
    apply_move(&c->state, &i, dt);

    int slot = (c->head + c->count) % WB_NET_MAX_INPUTS;
    c->ring[slot].seq = i.seq;
    c->ring[slot].in = i;
    c->ring[slot].after = c->state;
    if (c->count < WB_NET_MAX_INPUTS) c->count++;
    else c->head = (c->head + 1) % WB_NET_MAX_INPUTS;   /* overwrite oldest */

    if (c->send_fn) c->send_fn(c->send_ud, &i);
}

void wb_net_client_recv_snapshot(wb_net_client *c, const wb_net_snapshot *snap) {
    const wb_net_ent_state *mine = NULL;
    for (int i = 0; i < snap->count; i++)
        if (snap->ents[i].id == c->entity) { mine = &snap->ents[i]; break; }
    if (!mine) return;                       /* not in this snapshot */

    /* Find the prediction we made right after the acked input. */
    const wb_net_record *rec = NULL;
    for (int i = 0; i < c->count; i++) {
        int slot = (c->head + i) % WB_NET_MAX_INPUTS;
        if (c->ring[slot].seq == snap->ack_seq) { rec = &c->ring[slot]; break; }
    }

    float err = 0.0f;
    if (rec) {
        float dx = mine->pos[0] - rec->after.pos[0];
        float dy = mine->pos[1] - rec->after.pos[1];
        err = sqrtf(dx*dx + dy*dy);
    }
    c->last_error = err;

    if (err > WB_NET_EPS) {
        /* Correction: snap to the authoritative state, then replay every
         * input the server has not consumed yet. */
        c->state = *mine;
        for (int i = 0; i < c->count; i++) {
            int slot = (c->head + i) % WB_NET_MAX_INPUTS;
            if (c->ring[slot].seq <= snap->ack_seq) continue;
            apply_move(&c->state, &c->ring[slot].in, 1.0f / 20.0f);
        }
        c->corrections++;
    }

    /* Drop everything the server has acked. */
    while (c->count > 0 && c->ring[c->head].seq <= snap->ack_seq) {
        c->head = (c->head + 1) % WB_NET_MAX_INPUTS;
        c->count--;
    }
}

void wb_net_client_predicted(wb_net_client *c, wb_net_ent_state *out) { *out = c->state; }
int  wb_net_client_corrections(const wb_net_client *c) { return c->corrections; }
float wb_net_client_last_error(const wb_net_client *c) { return c->last_error; }
int  wb_net_client_pending_inputs(const wb_net_client *c) { return c->count; }

/* ==================================================================== */
/* loopback transport                                                    */
/* ==================================================================== */

typedef struct {
    int pending;
    wb_net_input in;
} wb_lo_input;

typedef struct {
    int pending;
    wb_net_snapshot snap;
} wb_lo_snap;

typedef struct wb_lo_binding {
    struct wb_net_loopback *lo;
    int client_id;
} wb_lo_binding;

struct wb_net_loopback {
    float loss_rate;
    uint32_t rng;
    wb_net_server *server[WB_NET_MAX_CLIENTS];
    wb_net_client *client[WB_NET_MAX_CLIENTS];
    wb_lo_binding *bindings[WB_NET_MAX_CLIENTS];
    wb_lo_input inq[WB_NET_MAX_CLIENTS];
    wb_lo_snap snapq[WB_NET_MAX_CLIENTS];
};

static void lo_send(void *ud, const wb_net_input *in) {
    wb_lo_binding *b = (wb_lo_binding *)ud;
    if (!b || !b->lo) return;
    b->lo->inq[b->client_id].in = *in;
    b->lo->inq[b->client_id].pending = 1;
}

wb_net_loopback *wb_net_loopback_new(float loss_rate) {
    wb_net_loopback *l = (wb_net_loopback *)calloc(1, sizeof *l);
    if (!l) return NULL;
    l->loss_rate = loss_rate < 0.0f ? 0.0f : (loss_rate > 1.0f ? 1.0f : loss_rate);
    l->rng = 12345u;
    return l;
}

void wb_net_loopback_free(wb_net_loopback *l) {
    if (!l) return;
    for (int i = 0; i < WB_NET_MAX_CLIENTS; i++) free(l->bindings[i]);
    free(l);
}

static int drop(wb_net_loopback *l) {
    if (l->loss_rate <= 0.0f) return 0;
    /* xorshift32 */
    l->rng ^= l->rng << 13; l->rng ^= l->rng >> 17; l->rng ^= l->rng << 5;
    float r = (float)(l->rng & 0xFFFFFF) / (float)0x1000000;
    return r < l->loss_rate;
}

void wb_net_loopback_connect(wb_net_loopback *l, int client_id,
                             wb_net_server *s, wb_net_client *c) {
    if (client_id < 0 || client_id >= WB_NET_MAX_CLIENTS) return;
    l->server[client_id] = s;
    l->client[client_id] = c;
    if (!l->bindings[client_id]) {
        l->bindings[client_id] = (wb_lo_binding *)calloc(1, sizeof(wb_lo_binding));
        l->bindings[client_id]->lo = l;
        l->bindings[client_id]->client_id = client_id;
    }
    wb_net_client_set_send_callback(c, lo_send, l->bindings[client_id]);
}

void wb_net_loopback_deliver(wb_net_loopback *l, int client_id) {
    if (client_id < 0 || client_id >= WB_NET_MAX_CLIENTS) return;
    if (l->inq[client_id].pending) {
        l->inq[client_id].pending = 0;
        if (!drop(l)) wb_net_server_recv_input(l->server[client_id], client_id,
                                              &l->inq[client_id].in);
    }
}

void wb_net_loopback_snapshot(wb_net_loopback *l, int client_id) {
    if (client_id < 0 || client_id >= WB_NET_MAX_CLIENTS) return;
    if (drop(l)) return;
    wb_net_snapshot snap;
    wb_net_server_snapshot(l->server[client_id], client_id, &snap);
    wb_net_client_recv_snapshot(l->client[client_id], &snap);
}

void wb_net_loopback_pump(wb_net_loopback *l, int client_id) {
    if (client_id < 0 || client_id >= WB_NET_MAX_CLIENTS) return;
    wb_net_server *s = l->server[client_id];
    wb_net_client *c = l->client[client_id];
    if (!s || !c) return;
    wb_net_loopback_deliver(l, client_id);
    wb_net_loopback_snapshot(l, client_id);
}
