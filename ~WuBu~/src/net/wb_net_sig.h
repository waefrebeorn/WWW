/*
 * wb_net_sig.h -- LP-95 stone 4: signalling, the half that moves candidates.
 *
 * STUN, ICE and TURN are all useless if the candidates never leave the host.
 * ICE is not a peer-to-peer protocol: the candidate lists and the ICE
 * credentials (RFC 8445 section 5.3, the username fragment and password that
 * authenticate a connectivity check) have to be carried by some other channel --
 * WebSocket, an HTTP offer/answer, a QR code on a LAN. This is the wire format
 * for that exchange, plus the connect-token gateway that lets two clients find
 * each other in the first place.
 *
 * The candidate record is fixed-width so it can be memcmp'd and so no length
 * field can disagree with the count. A whole offer fits in one datagram.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_SIG_H
#define WB_NET_SIG_H

#include "wb_ice.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ICE credentials. RFC 8445 section 5.3: the username is
 * "<remote-ufrag>:<local-ufrag>" and the password is a long random string. */
#define WB_ICE_UFRAG_MAX   24
#define WB_ICE_PASSWORD_MAX 64

typedef struct {
    char ufrag[WB_ICE_UFRAG_MAX];
    char password[WB_ICE_PASSWORD_MAX];
    int  controlling;
} wb_ice_creds;

/* Enough candidates to fit the record block in one datagram alongside the
 * header. The encoder refuses more rather than emitting a truncated offer. */
#define WB_ICE_SIG_MAX_CANDIDATES 21
#define WB_ICE_SIG_HEADER        95
#define WB_ICE_SIG_RECORD        52

enum {
    WB_ICE_SIG_OFFER = 0,
    WB_ICE_SIG_ANSWER = 1
};

/* Encode an offer or an answer. Returns the length, or 0 if it does not fit or
 * the candidate list is too long. */
size_t wb_ice_sig_encode(uint8_t *out, size_t cap, const wb_ice_creds *creds,
                         const wb_ice_candidate *cands, size_t n, int answer);

/* Decode one. Returns 0 on success. -1 if the buffer is not a well-formed
 * offer: bad magic, bad version, a count that disagrees with the length, or a
 * record whose address family is neither 4 nor 6. */
int wb_ice_sig_decode(const uint8_t *buf, size_t len, wb_ice_creds *creds,
                      wb_ice_candidate *cands, size_t max, size_t *n);

/* ---- the connect-token gateway ---- */

typedef struct wb_net_gateway wb_net_gateway;

/* `lifetime` is how long an issued token stays redeemable, in seconds. */
wb_net_gateway *wb_net_gateway_new(uint32_t lifetime);
void wb_net_gateway_free(wb_net_gateway *g);

/* Issue a token for a room. Two clients who hold tokens from the same room can
 * find each other. Deterministic for a given room and issue count, so a token
 * can be reconstructed rather than stored. */
void wb_net_gateway_issue(wb_net_gateway *g, const char *room, uint8_t token[16]);

/* Redeem a token. Returns 1 if it was accepted (first or second peer), 0 if it
 * is unknown, expired, or this peer has already redeemed it. */
int wb_net_gateway_redeem(wb_net_gateway *g, const uint8_t token[16], int peer);

/* Once two peers have redeemed, the room is connected. */
int wb_net_gateway_connected(wb_net_gateway *g, const uint8_t token[16]);

/* The OTHER peer in the room, or -1. */
int wb_net_gateway_peer(wb_net_gateway *g, const uint8_t token[16], int peer);

/* Advance the clock: tokens expire. */
void wb_net_gateway_tick(wb_net_gateway *g, double dt);

unsigned long long wb_net_gateway_issued(const wb_net_gateway *g);
unsigned long long wb_net_gateway_connected_rooms(const wb_net_gateway *g);
unsigned long long wb_net_gateway_refused(const wb_net_gateway *g);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_SIG_H */
