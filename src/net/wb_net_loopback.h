/*
 * wb_net_loopback.h -- WuBu Engine in-process transport for tests / local play.
 * See wb_net.h.
 */
#ifndef WB_NET_LOOPBACK_H
#define WB_NET_LOOPBACK_H

#include "wb_net.h"

typedef struct wb_net_server wb_net_server;
typedef struct wb_net_client wb_net_client;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_net_loopback wb_net_loopback;

wb_net_loopback *wb_net_loopback_new(float loss_rate /* 0..1 */);
void wb_net_loopback_free(wb_net_loopback *l);

/* Connect a client to a server: inputs flow client->server, snapshots flow
 * server->client, with `loss_rate` of packets dropped in each direction. */
void wb_net_loopback_connect(wb_net_loopback *l, int client_id,
                             wb_net_server *s, wb_net_client *c);
/* Deliver any queued client input to the server (no snapshot). Call this
 * BEFORE wb_net_server_tick so the server simulates the same tick the client
 * sampled -- otherwise the server lags a tick behind every prediction. */
void wb_net_loopback_deliver(wb_net_loopback *l, int client_id);

/* Send the server's snapshot for a client (AOI + delta applied). */
void wb_net_loopback_snapshot(wb_net_loopback *l, int client_id);

/* Deliver + tick-equivalent + snapshot in one call. */
void wb_net_loopback_pump(wb_net_loopback *l, int client_id);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_LOOPBACK_H */
