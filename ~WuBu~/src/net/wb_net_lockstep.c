/*
 * wb_net_lockstep.c -- the lockstep frame-stepper. See wb_net_lockstep.h.
 *
 * Wire payloads, carried as REL messages over the link:
 *   'I'  u32 tick, i32 peer, f32 move_x, f32 move_y, f32 yaw, i32 jump
 *   'D'  u32 tick, u64 digest
 */
#include "wb_net_lockstep.h"

#include <stdlib.h>
#include <string.h>

#define WB_LS_KIND_INPUT  'I'
#define WB_LS_KIND_DIGEST 'D'
#define WB_LS_PAYLOAD_INPUT  25
#define WB_LS_PAYLOAD_DIGEST 13

struct wb_net_lockstep {
    int          local_peer, far_peer;
    float        tick_rate;
    float        dt;
    uint32_t     tick;              /* the next tick to step */
    uint32_t     stalled;           /* step() calls that found the peer missing */
    int          desynced;
    uint32_t     desync_tick;

    wb_net_input win[WB_LOCKSTEP_WINDOW][2];
    uint8_t      have[WB_LOCKSTEP_WINDOW][2];
    uint64_t     local_digest[WB_LOCKSTEP_WINDOW];
    uint8_t      have_local[WB_LOCKSTEP_WINDOW];
    uint64_t     remote_digest[WB_LOCKSTEP_WINDOW];
    uint8_t      have_remote[WB_LOCKSTEP_WINDOW];

    wb_net_reliable *net;
};

static uint32_t ls_slot(uint32_t tick) { return tick % WB_LOCKSTEP_WINDOW; }

static uint32_t ls_f2u(float f) { uint32_t u; memcpy(&u, &f, sizeof u); return u; }
static float    ls_u2f(uint32_t u) { float f; memcpy(&f, &u, sizeof u); return f; }

static void ls_compare(wb_net_lockstep *ls, uint32_t tick) {
    uint32_t s = ls_slot(tick);
    if (!ls->have_local[s] || !ls->have_remote[s]) return;
    if (ls->local_digest[s] != ls->remote_digest[s] && !ls->desynced) {
        ls->desynced = 1;
        ls->desync_tick = tick;
    }
}

static int ls_send_input(wb_net_lockstep *ls, uint32_t tick,
                         const wb_net_input *in) {
    uint8_t b[WB_LS_PAYLOAD_INPUT];
    memset(b, 0, sizeof b);
    b[0] = WB_LS_KIND_INPUT;
    wb_net_put_u32(b + 1, tick);
    wb_net_put_u32(b + 5, (uint32_t)ls->local_peer);
    wb_net_put_u32(b + 9,  ls_f2u(in->move_x));
    wb_net_put_u32(b + 13, ls_f2u(in->move_y));
    wb_net_put_u32(b + 17, ls_f2u(in->yaw));
    wb_net_put_u32(b + 21, (uint32_t)in->jump);
    return wb_net_reliable_send(ls->net, ls->far_peer, WB_NET_CLASS_REL,
                                b, sizeof b);
}

static int ls_send_digest(wb_net_lockstep *ls, uint32_t tick, uint64_t d) {
    uint8_t b[WB_LS_PAYLOAD_DIGEST];
    memset(b, 0, sizeof b);
    b[0] = WB_LS_KIND_DIGEST;
    wb_net_put_u32(b + 1, tick);
    wb_net_put_u32(b + 5, (uint32_t)(d & 0xFFFFFFFFu));
    wb_net_put_u32(b + 9, (uint32_t)(d >> 32));
    return wb_net_reliable_send(ls->net, ls->far_peer, WB_NET_CLASS_REL,
                                b, sizeof b);
}

static int ls_store_input(wb_net_lockstep *ls, uint32_t tick, int peer,
                          const wb_net_input *in) {
    if (tick < ls->tick || tick >= ls->tick + WB_LOCKSTEP_WINDOW) return 0;
    uint32_t s = ls_slot(tick);
    ls->win[s][peer] = *in;
    ls->have[s][peer] = 1;
    return 1;
}

static void ls_recv(void *ud, int peer, wb_net_class cls,
                    const void *data, uint16_t len) {
    (void)cls;
    (void)peer;
    wb_net_lockstep *ls = (wb_net_lockstep *)ud;
    if (!ls || !data) return;
    const uint8_t *b = (const uint8_t *)data;

    if (b[0] == WB_LS_KIND_INPUT) {
        if (len != WB_LS_PAYLOAD_INPUT) return;
        uint32_t tick = wb_net_get_u32(b + 1);
        int peer_id = (int)wb_net_get_u32(b + 5);
        if (peer_id != 0 && peer_id != 1) return;
        wb_net_input in;
        memset(&in, 0, sizeof in);
        in.tick   = tick;
        in.move_x = ls_u2f(wb_net_get_u32(b + 9));
        in.move_y = ls_u2f(wb_net_get_u32(b + 13));
        in.yaw    = ls_u2f(wb_net_get_u32(b + 17));
        in.jump   = (int)wb_net_get_u32(b + 21);
        ls_store_input(ls, tick, peer_id, &in);
        return;
    }
    if (b[0] == WB_LS_KIND_DIGEST) {
        if (len != WB_LS_PAYLOAD_DIGEST) return;
        uint32_t tick = wb_net_get_u32(b + 1);
        uint64_t d = (uint64_t)wb_net_get_u32(b + 5) |
                     ((uint64_t)wb_net_get_u32(b + 9) << 32);
        if (tick >= ls->tick + WB_LOCKSTEP_WINDOW) return;
        uint32_t s = ls_slot(tick);
        ls->remote_digest[s] = d;
        ls->have_remote[s] = 1;
        ls_compare(ls, tick);
    }
}

wb_net_lockstep *wb_net_lockstep_new(int local_peer, float tick_rate,
                                     wb_net_reliable *net) {
    if (!net) return NULL;
    if (local_peer != 0 && local_peer != 1) return NULL;
    if (tick_rate <= 0.0f) return NULL;
    wb_net_lockstep *ls = (wb_net_lockstep *)calloc(1, sizeof *ls);
    if (!ls) return NULL;
    ls->local_peer = local_peer;
    ls->far_peer = 1 - local_peer;
    ls->tick_rate = tick_rate;
    ls->dt = 1.0f / tick_rate;
    ls->net = net;
    return ls;
}

void wb_net_lockstep_free(wb_net_lockstep *ls) { free(ls); }

int wb_net_lockstep_submit(wb_net_lockstep *ls, uint32_t tick,
                           const wb_net_input *in) {
    if (!ls || !in) return -1;
    if (tick < ls->tick || tick >= ls->tick + WB_LOCKSTEP_WINDOW) return -1;
    uint32_t s = ls_slot(tick);
    ls->win[s][ls->local_peer] = *in;
    ls->have[s][ls->local_peer] = 1;
    if (ls_send_input(ls, tick, in) != 0) {
        ls->have[s][ls->local_peer] = 0;    /* the send failed: do not step on it */
        return -1;
    }
    return 0;
}

int wb_net_lockstep_step(wb_net_lockstep *ls, wb_lockstep_step_fn fn, void *ud) {
    if (!ls) return 0;
    uint32_t t = ls->tick;
    uint32_t s = ls_slot(t);
    if (!ls->have[s][0] || !ls->have[s][1]) {
        ls->stalled++;
        return 0;
    }
    wb_net_input in[2];
    in[0] = ls->win[s][0];
    in[1] = ls->win[s][1];
    ls->have[s][0] = ls->have[s][1] = 0;
    ls->have_local[s] = ls->have_remote[s] = 0;   /* slot is spent */
    ls->tick = t + 1;
    if (fn) fn(ud, t, in, 2);
    return 1;
}

void wb_net_lockstep_digest(wb_net_lockstep *ls, uint32_t tick, uint64_t digest) {
    if (!ls) return;
    if (tick >= ls->tick + WB_LOCKSTEP_WINDOW) return;
    uint32_t s = ls_slot(tick);
    ls->local_digest[s] = digest;
    ls->have_local[s] = 1;
    if (ls_send_digest(ls, tick, digest) != 0) return;
    ls_compare(ls, tick);
}

int wb_net_lockstep_desynced(const wb_net_lockstep *ls, uint32_t *tick) {
    if (!ls) return 0;
    if (tick) *tick = ls->desync_tick;
    return ls->desynced;
}

uint32_t wb_net_lockstep_tick(const wb_net_lockstep *ls) { return ls ? ls->tick : 0; }
float    wb_net_lockstep_dt(const wb_net_lockstep *ls)    { return ls ? ls->dt : 0.0f; }
uint32_t wb_net_lockstep_stalled(const wb_net_lockstep *ls) { return ls ? ls->stalled : 0; }

int wb_net_lockstep_poll(wb_net_lockstep *ls) {
    return ls ? wb_net_reliable_poll(ls->net, ls_recv, ls) : 0;
}
