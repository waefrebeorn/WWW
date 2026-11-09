/*
 * wb_net.h -- WuBu Engine networking core: shared wire types and AOI.
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
 * The server, client and loopback transports have their own headers
 * (wb_net_server.h, wb_net_client.h, wb_net_loopback.h) so a caller pulls in
 * only the half it uses.
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

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_H */
