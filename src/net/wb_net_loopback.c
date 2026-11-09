/*
 * wb_net_loopback.c -- in-process transport: inputs client->server, snapshots
 * server->client, with a deterministic loss model. See wb_net_loopback.h.
 */
#include "wb_net_internal.h"

#include <stdlib.h>

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
