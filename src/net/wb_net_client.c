/*
 * wb_net_client.c -- client-side prediction and reconciliation. See
 * wb_net_client.h.
 */
#include "wb_net_internal.h"

#include <math.h>
#include <stdlib.h>

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
