/*
 * wb_net_internal.h -- private state of the networking module.
 *
 * Included only by the wb_net*.c translation units. The public headers keep
 * wb_net_server / wb_net_client / wb_net_loopback opaque; this is where the
 * structs live, so no caller can reach into them.
 */
#ifndef WB_NET_INTERNAL_H
#define WB_NET_INTERNAL_H

#include "wb_net.h"
#include "wb_net_client.h"
#include "wb_net_loopback.h"
#include "wb_net_server.h"

#define WB_NET_SPEED 5.0f      /* m/s, matches the character controller walk */
#define WB_NET_EPS   1e-4f
#define WB_NET_DELTA_EPS 0.001f

/* ---------------------------------------------------------------- server */

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

struct wb_net_server {
    wb_net_ent ents[WB_NET_MAX_ENTITIES];
    uint32_t   next_id;
    uint32_t   tick;
    float      tick_rate;
    float      aoi_radius;
    wb_net_last last[WB_NET_MAX_CLIENTS][WB_NET_MAX_ENTITIES];
};

/* ---------------------------------------------------------------- client */

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

/* -------------------------------------------------------------- loopback */

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

#endif /* WB_NET_INTERNAL_H */
