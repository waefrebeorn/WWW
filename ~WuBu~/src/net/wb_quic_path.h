/*
 * wb_quic_path.h -- LP-93 stone 13: path validation and migration.
 *
 * RFC 9000 section 8.2 and section 9. A connection can move: the local address
 * can change, or the peer's. Path validation is what tells a migrating peer
 * from a spoofed source address -- an acknowledgment cannot, because it carries
 * too little entropy and can be forged.
 *
 * The exchange: the initiator sends PATH_CHALLENGE with unpredictable data;
 * the receiver echoes it in PATH_RESPONSE on the path the challenge arrived on;
 * the initiator validates the path when a PATH_RESPONSE carrying that data
 * arrives. A response received on any path validates the path the challenge
 * went out on -- the initiator must not check the return path, or it would open
 * itself to the off-path forwarding attack of section 9.3.3.
 *
 * Two rules worth stating plainly:
 *
 *   - A datagram carrying a PATH_CHALLENGE or PATH_RESPONSE is expanded to at
 *     least 1200 bytes so the path's MTU is verified too. A challenge sent
 *     unexpanded validates the address but not the MTU (section 8.2.3), and the
 *     endpoint must validate again with an expanded datagram.
 *   - Validation fails only when the endpoint abandons it, after roughly three
 *     times the larger of the current PTO and the new path's PTO. Losing one
 *     challenge is not failure, which is why several can be outstanding.
 *
 * The challenge data comes from the caller's RNG; this module never invents it.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_PATH_H
#define WB_QUIC_PATH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_QUIC_PATH_DATA_LEN 8
#define WB_QUIC_PATH_MAX 8

/* The smallest allowed maximum datagram size, RFC 9000 section 14.1. A
 * challenge or response must go out in a datagram at least this large unless
 * the anti-amplification limit forbids it. */
#define WB_QUIC_PATH_MIN_DATAGRAM 1200

typedef struct {
    uint8_t data[WB_QUIC_PATH_DATA_LEN];
    uint64_t sent_us;
    int      used;
    int      answered;
    int      expanded;   /* sent in a datagram of at least 1200 bytes */
} wb_quic_probe;

typedef struct {
    wb_quic_probe probes[WB_QUIC_PATH_MAX];
    uint64_t now_us;
    int      validated;
    int      mtu_validated;
    int      abandoned;
} wb_quic_path;

void wb_quic_path_init(wb_quic_path *p, uint64_t now_us);

/* Begin a validation with the caller's unpredictable `data`. `expanded` records
 * whether the datagram carrying it reaches WB_QUIC_PATH_MIN_DATAGRAM. Returns 0
 * on success, -1 if there is no room for another probe. */
int wb_quic_path_start(wb_quic_path *p, const uint8_t data[WB_QUIC_PATH_DATA_LEN],
                       int expanded);

/* A PATH_RESPONSE arrived carrying `data`. Returns 1 if it validated the path,
 * 0 if it matched an outstanding challenge but the path was already validated,
 * and -1 if it matched nothing -- an unsolicited response, which is ignored. */
int wb_quic_path_response(wb_quic_path *p, const uint8_t data[WB_QUIC_PATH_DATA_LEN]);

/* Expire probes older than three times `pto_us`. The path is abandoned only
 * when the last outstanding probe goes, so losing one challenge is not a
 * failure. Returns 1 if this call abandoned the path, 0 otherwise. */
int wb_quic_path_tick(wb_quic_path *p, uint64_t pto_us);

/* Give up on the path without waiting for the timer -- what an endpoint does
 * when it migrates away and abandons validation on the old path. */
void wb_quic_path_abandon(wb_quic_path *p);

int wb_quic_path_validated(const wb_quic_path *p);
int wb_quic_path_mtu_validated(const wb_quic_path *p);
int wb_quic_path_abandoned(const wb_quic_path *p);
int wb_quic_path_probe_count(const wb_quic_path *p);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_PATH_H */
