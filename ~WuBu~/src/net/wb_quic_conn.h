/*
 * wb_quic_conn.h -- LP-93 stone 6: the three packet number spaces as one
 * connection, and key discard.
 *
 * RFC 9000 keeps three independent packet number spaces -- Initial, Handshake
 * and Application -- and stone 4 gave one of them its own loss logic. This is
 * the object that holds all three, so a transport can ask which space a packet
 * belongs to and route the right acknowledgements to it.
 *
 * Each space carries its own packet protection keys (RFC 9001 section 4.9).
 * When TLS discards them, RFC 9001 section 4.9.1 says the loss recovery state
 * for that encryption level is abandoned and any outstanding packets are
 * ignored:
 *
 *   - a client discards Initial keys when it first sends a Handshake packet;
 *   - a server discards them when it first successfully processes one;
 *   - Handshake keys are discarded when the handshake is confirmed (4.9.2).
 *
 * The packet number itself is NOT reset. RFC 9000 section 17.1: "A QUIC
 * endpoint MUST NOT reuse a packet number within the same packet number space
 * in one connection." A discarded space continues from where it stopped.
 *
 * 0-RTT and 1-RTT share the application space (RFC 9001 section 4.9.3), so
 * there is no separate 0-RTT space here.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_CONN_H
#define WB_QUIC_CONN_H

#include <stddef.h>
#include <stdint.h>

#include "wb_quic_space.h"
#include "wb_quic_protect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WB_QUIC_SP_INITIAL = 0,
    WB_QUIC_SP_HANDSHAKE,
    WB_QUIC_SP_APPLICATION,
    WB_QUIC_SP_COUNT
} wb_quic_sp;

typedef struct {
    int     installed;
    uint8_t secret[WB_QUIC_SECRET_LEN];
    uint8_t key[WB_QUIC_KEY_LEN];
    uint8_t iv[WB_QUIC_IV_LEN];
    uint8_t hp[WB_QUIC_HP_LEN];
} wb_quic_key_set;

typedef struct {
    wb_quic_space   sp[WB_QUIC_SP_COUNT];
    wb_quic_key_set keys[WB_QUIC_SP_COUNT];
    uint64_t        now_us;
} wb_quic_conn;

/* A connection with no keys and three empty spaces. */
void wb_quic_conn_init(wb_quic_conn *c, uint64_t now_us);

/* Advance the injected clock in all three spaces. */
void wb_quic_conn_tick(wb_quic_conn *c, uint64_t now_us);

/* Install one space's packet protection keys: the HKDF output and the three
 * expand-label products. */
void wb_quic_conn_install_keys(wb_quic_conn *c, wb_quic_sp sp,
                               const uint8_t *secret, const uint8_t *key,
                               const uint8_t *iv, const uint8_t *hp);

/* Whether a space has keys. */
int wb_quic_conn_has_keys(const wb_quic_conn *c, wb_quic_sp sp);

/* The keys for a space, or NULL. */
const wb_quic_key_set *wb_quic_conn_keys(const wb_quic_conn *c, wb_quic_sp sp);

/* RFC 9001 section 4.9: abandon a space's loss recovery state and drop its
 * keys. The packet number continues, because RFC 9000 section 17.1 forbids
 * reuse. The other spaces are untouched. */
void wb_quic_conn_discard_space(wb_quic_conn *c, wb_quic_sp sp);

/* Route a send to a space. */
uint64_t wb_quic_conn_send(wb_quic_conn *c, wb_quic_sp sp, uint32_t bytes,
                           int ack_eliciting, int padding);

/* Route an acknowledgement to a space. Returns as wb_quic_space_acked. */
int wb_quic_conn_acked(wb_quic_conn *c, wb_quic_sp sp, uint64_t pn);

/* The loss set of a space. */
size_t wb_quic_conn_lost(wb_quic_conn *c, wb_quic_sp sp, uint64_t *pns,
                         size_t max, uint64_t *loss_time);

/* The probe timeout of a space. */
uint64_t wb_quic_conn_pto(wb_quic_conn *c, wb_quic_sp sp,
                          uint64_t max_ack_delay_us, int confirmed, int backoff);

/* Whether a space has run out of packet numbers. */
int wb_quic_conn_pn_exhausted(const wb_quic_conn *c, wb_quic_sp sp);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_CONN_H */
