/*
 * wb_net_udp.h -- LP-70 stone 2: the UDP socket transport.
 *
 * The same `wb_net_transport` contract the in-process pair implements, over a
 * real socket: send queues a datagram to a peer, poll delivers whatever has
 * arrived, and a datagram the socket refuses (or the network drops) is
 * indistinguishable from a delivered one -- which is exactly what the
 * reliability layer exists to compensate for.
 *
 * Nothing here sequences or retransmits. `wb_net_reliable` wraps it.
 *
 * Win32 winsock2. C11.
 */
#ifndef WB_NET_UDP_H
#define WB_NET_UDP_H

#include <stddef.h>
#include <stdint.h>

#include "wb_net_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_net_udp wb_net_udp;

/* Bind a UDP socket. `port` 0 picks an ephemeral port, which is what two local
 * endpoints use; the chosen port is reported by wb_net_udp_bound_port. `err`
 * receives a message on failure. */
wb_net_udp *wb_net_udp_new(uint16_t port, char *err, size_t errn);

/* Free the endpoint. */
void wb_net_udp_destroy(wb_net_udp *u);

/* The port actually bound (useful when `port` was 0). */
uint16_t wb_net_udp_bound_port(const wb_net_udp *u);

/* Address a peer. `peer` is the id send/poll use; ids need not be dense.
 * `host` is a dotted-quad or a name. Returns 0 on success. */
int wb_net_udp_add_peer(wb_net_udp *u, int peer, const char *host, uint16_t port);

/* Drop every addressed peer (the socket stays bound). */
void wb_net_udp_clear_peers(wb_net_udp *u);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_UDP_H */
