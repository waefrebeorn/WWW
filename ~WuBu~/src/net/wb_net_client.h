/*
 * wb_net_client.h -- WuBu Engine predicting client. See wb_net.h.
 */
#ifndef WB_NET_CLIENT_H
#define WB_NET_CLIENT_H

#include "wb_net.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_CLIENT_H */
