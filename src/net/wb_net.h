/*
 * wb_net.h -- WuBu Engine networking core (slice 13).
 *
 * Server-authoritative multiplayer: clients send inputs, the server simulates
 * and sends snapshots, clients predict their own movement and reconcile
 * against the authoritative state. Interest management (AOI) runs on the
 * engine's existing quadkey spatial core -- the same index that drives
 * streaming and sharding -- so bandwidth is O(nearby), not O(world).
 *
 * Transport is abstract (`wb_net_transport`) so the same netcode runs over UDP,
 * a loopback pipe (tests), or a relay. Nothing here touches sockets.
 *
 * Design follows the standard client-prediction/reconciliation model used by
 * shipped client-server games: tick-stamped inputs, a ring buffer of
 * (seq, input, predicted state), rewind-and-replay on correction.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_H
#define WB_NET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_NET_MAX_ENTITIES   1024
#define WB_NET_MAX_INPUTS     256      /* client input ring buffer */
#define WB_NET_MAX_CLIENTS    64
#define WB_NET_MAX_PACKET     1400

/* One simulation tick's worth of player intent. */
typedef struct {
    uint32_t seq;          /* client-assigned, monotonic */
    uint32_t tick;         /* client tick it was sampled at */
    float    move_x;       /* -1..1, camera relative */
    float    move_y;
    float    yaw;          /* desired facing */
    int      jump;
} wb_net_input;

/* Replicated entity state (what goes on the wire). */
typedef struct {
    uint32_t id;
    float    pos[3];
    float    vel[3];
    float    yaw;
    uint8_t  flags;        /* bit0: grounded */
} wb_net_ent_state;

/* A snapshot for one client. */
typedef struct {
    uint32_t tick;               /* server tick */
    uint32_t ack_seq;            /* last client input seq consumed */
    int      count;
    wb_net_ent_state ents[WB_NET_MAX_ENTITIES];
} wb_net_snapshot;

/* ---- AOI (interest management on the quadkey) -------------------------- */

/* Choose the quadkey level whose cell diagonal is about `radius` metres. */
int  wb_net_aoi_level(float radius_m);

/* 1 if two lon/lat points are within `radius` metres of each other, decided
 * with quadkey cells at the matching level (so the test is the same code path
 * the world uses). */
int  wb_net_aoi_visible(const double origin_lonlat[2], const double pos_lonlat[2],
                        float radius_m);

/* ---- server ------------------------------------------------------------ */

typedef struct wb_net_server wb_net_server;

wb_net_server *wb_net_server_new(float tick_rate);
void wb_net_server_free(wb_net_server *s);

/* Simulate one server tick. */
void wb_net_server_tick(wb_net_server *s);

/* Feed a client's input (called by the transport layer). */
void wb_net_server_recv_input(wb_net_server *s, int client_id, const wb_net_input *in);

/* Build the snapshot for a client: their own entity (authoritative) plus
 * every entity inside their AOI radius. `out` is cleared first. */
void wb_net_server_snapshot(wb_net_server *s, int client_id, wb_net_snapshot *out);

uint32_t wb_net_server_tick_count(const wb_net_server *s);
int      wb_net_server_entity_count(const wb_net_server *s);

/* Entity ids are assigned by the server. Ids start at 1: 0 is reserved as the
 * invalid/none sentinel. */
uint32_t wb_net_server_add_entity(wb_net_server *s, const double lonlat[2],
                                  const float pos[3]);

/* Add a player entity bound to a client id (the entity the server will
 * reconcile against that client's prediction). */
uint32_t wb_net_server_add_player(wb_net_server *s, int client_id,
                                  const double lonlat[2], const float pos[3]);

/* Metres; entities beyond this from the client are not replicated. */
void wb_net_server_set_aoi_radius(wb_net_server *s, float radius_m);

/* Server-authoritative displacement (knockback, a hit, a teleporter). This is
 * the event that makes a client's prediction wrong and forces a correction. */
void wb_net_server_apply_impulse(wb_net_server *s, uint32_t id, float dx, float dy);
void     wb_net_server_remove_entity(wb_net_server *s, uint32_t id);
wb_net_ent_state wb_net_server_entity(wb_net_server *s, uint32_t id);

/* ---- client ------------------------------------------------------------ */

typedef struct wb_net_client wb_net_client;

wb_net_client *wb_net_client_new(uint32_t client_entity, float aoi_radius);
void wb_net_client_free(wb_net_client *c);

/* Sample this tick's intent: assigns the next seq, applies it to the predicted
 * state immediately, and stores it for later reconciliation. If a send
 * callback is installed (see wb_net_client_set_send_callback) the input is
 * handed to the transport as well. */
void wb_net_client_apply_input(wb_net_client *c, const wb_net_input *in, float dt);

/* The transport the client sends inputs through. `ud` is the transport. */
typedef void (*wb_net_send_fn)(void *ud, const wb_net_input *in);
void wb_net_client_set_send_callback(wb_net_client *c, wb_net_send_fn fn, void *ud);

/* Apply a snapshot: reconcile the client's own entity. */
void wb_net_client_recv_snapshot(wb_net_client *c, const wb_net_snapshot *snap);

/* Current predicted state of the client's own entity. */
void wb_net_client_predicted(wb_net_client *c, wb_net_ent_state *out);

/* How many corrections have been applied since creation. */
int  wb_net_client_corrections(const wb_net_client *c);
/* The last prediction error magnitude (useful for telemetry). */
float wb_net_client_last_error(const wb_net_client *c);
/* Inputs still awaiting acknowledgement. */
int  wb_net_client_pending_inputs(const wb_net_client *c);

/* ---- loopback transport (tests / local play) --------------------------- */

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

#endif /* WB_NET_H */
