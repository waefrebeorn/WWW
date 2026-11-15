/*
 * wb_turn.h -- LP-95 stone 3: TURN, the relay for symmetric NATs.
 *
 * STUN finds a reflexive address and ICE picks the best path. When both ends
 * are behind symmetric NATs, neither works: the NAT allocates a different
 * mapping per destination, so the address one peer learns is not the address
 * the other can send to. TURN (RFC 8656) is the fallback. The client asks a
 * relay for an address, the relay allocates one, and both peers send to the
 * relay, which forwards.
 *
 * The four methods, and why each exists:
 *
 *   Allocate     get a relayed address. Without it there is nothing to send to.
 *   Refresh      keep an allocation alive; they expire.
 *   CreatePermission
 *                the relay only forwards from a peer it has been told about --
 *                otherwise it is an open relay, which is an abuse amplifier.
 *   ChannelBind  a short 16-bit channel number in place of a full address on
 *                the forward path, so the hot path is 4 bytes shorter.
 *
 * Send and Data are indications, not requests: there is nothing to acknowledge,
 * and a retransmit would duplicate a packet the peer may already have.
 *
 * C11, no external deps.
 */
#ifndef WB_TURN_H
#define WB_TURN_H

#include "wb_stun.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    WB_TURN_ALLOCATE_REQUEST = 0x0003,
    WB_TURN_ALLOCATE_SUCCESS = 0x0103,
    WB_TURN_ALLOCATE_ERROR   = 0x0113,
    WB_TURN_REFRESH_REQUEST  = 0x0004,
    WB_TURN_REFRESH_SUCCESS  = 0x0104,
    WB_TURN_REFRESH_ERROR    = 0x0114,
    WB_TURN_CREATE_PERMISSION_REQUEST = 0x0008,
    WB_TURN_CREATE_PERMISSION_SUCCESS = 0x0108,
    WB_TURN_CREATE_PERMISSION_ERROR   = 0x0118,
    WB_TURN_CHANNEL_BIND_REQUEST = 0x0009,
    WB_TURN_CHANNEL_BIND_SUCCESS   = 0x0109,
    WB_TURN_CHANNEL_BIND_ERROR     = 0x0119,
    WB_TURN_SEND_INDICATION = 0x0016,
    WB_TURN_DATA_INDICATION = 0x0017
};

enum {
    WB_TURN_ATTR_CHANNEL_NUMBER      = 0x000C,
    WB_TURN_ATTR_LIFETIME            = 0x000D,
    WB_TURN_ATTR_XOR_PEER_ADDRESS    = 0x0012,
    WB_TURN_ATTR_DATA                = 0x0013,
    WB_TURN_ATTR_XOR_RELAYED_ADDRESS = 0x0016,
    WB_TURN_ATTR_REQUESTED_TRANSPORT = 0x0019,
    WB_TURN_ATTR_DONT_FRAGMENT       = 0x001A
};

#define WB_TURN_LIFETIME_DEFAULT      600u
#define WB_TURN_PERMISSION_LIFETIME   300u
#define WB_TURN_CHANNEL_LIFETIME      600u
#define WB_TURN_CHANNEL_MIN           0x4000u
#define WB_TURN_CHANNEL_MAX           0x7FFEu
#define WB_TURN_TRANSPORT_UDP         17u

/* ---- the client side: build requests ---- */

size_t wb_turn_build_allocate(uint8_t *out, size_t cap,
                              const uint8_t txid[WB_STUN_TXID_LEN], int udp);
size_t wb_turn_build_refresh(uint8_t *out, size_t cap,
                             const uint8_t txid[WB_STUN_TXID_LEN],
                             uint32_t lifetime);
size_t wb_turn_build_create_permission(uint8_t *out, size_t cap,
                                       const uint8_t txid[WB_STUN_TXID_LEN],
                                       const wb_stun_addr *peer);
size_t wb_turn_build_channel_bind(uint8_t *out, size_t cap,
                                  const uint8_t txid[WB_STUN_TXID_LEN],
                                  uint16_t channel, const wb_stun_addr *peer);
size_t wb_turn_build_send(uint8_t *out, size_t cap,
                          const wb_stun_addr *peer,
                          const void *data, size_t len);

/* Parse an Allocate success: the relayed address and the lifetime. */
int wb_turn_parse_allocate_success(const uint8_t *buf, size_t len,
                                   wb_stun_addr *relayed, uint32_t *lifetime);

int wb_turn_is_request(uint16_t type);
int wb_turn_is_indication(uint16_t type);
int wb_turn_is_success(uint16_t type);
int wb_turn_is_error(uint16_t type);

/* ---- the server side ---- */

typedef struct wb_turn_server wb_turn_server;

/* `relay_base` is the address handed to clients; ports are allocated from
 * `relay_port_base` upward, one per allocation. `lifetime` is the allocation
 * lifetime in seconds. */
wb_turn_server *wb_turn_server_new(const wb_stun_addr *relay_base,
                                   uint16_t relay_port_base,
                                   uint32_t lifetime);
void wb_turn_server_free(wb_turn_server *s);

typedef int (*wb_turn_send_fn)(void *ud, int peer, const uint8_t *data, size_t len);

/* Data the relay forwards. `from` is the peer it came from. */
typedef void (*wb_turn_deliver_fn)(void *ud, int peer, const wb_stun_addr *from,
                                   const uint8_t *data, size_t len);

/* Handle one datagram. If it is a request, a response is written to `out` and
 * handed to `send`. If it is a Send indication that a permission allows, the
 * payload is handed to `deliver`. Returns 1 if anything happened, 0 if the
 * datagram was not TURN traffic. */
int wb_turn_server_handle(wb_turn_server *s, const uint8_t *buf, size_t len,
                          int peer, const wb_stun_addr *observed,
                          wb_turn_send_fn send, void *send_ud,
                          uint8_t *out, size_t cap,
                          wb_turn_deliver_fn deliver, void *deliver_ud);

/* Advance the server clock: allocations, permissions and channel bindings all
 * expire. */
void wb_turn_server_tick(wb_turn_server *s, double dt);

unsigned long long wb_turn_server_allocations(const wb_turn_server *s);
unsigned long long wb_turn_server_permissions(const wb_turn_server *s);
unsigned long long wb_turn_server_forwards(const wb_turn_server *s);
unsigned long long wb_turn_server_refused(const wb_turn_server *s);

#ifdef __cplusplus
}
#endif

#endif /* WB_TURN_H */
