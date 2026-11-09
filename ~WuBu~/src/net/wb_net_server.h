/*
 * wb_net_server.h -- WuBu Engine authoritative server. See wb_net.h.
 */
#ifndef WB_NET_SERVER_H
#define WB_NET_SERVER_H

#include "wb_net.h"

#ifdef __cplusplus
extern "C" {
#endif

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
void     wb_net_server_apply_impulse(wb_net_server *s, uint32_t id, float dx, float dy);
void     wb_net_server_remove_entity(wb_net_server *s, uint32_t id);
wb_net_ent_state wb_net_server_entity(wb_net_server *s, uint32_t id);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_SERVER_H */
