/*
 * wb_quic_conn.c -- LP-93 stone 6: the three packet number spaces as one
 * connection, and key discard.
 */
#include "wb_quic_conn.h"

#include <string.h>

void wb_quic_conn_init(wb_quic_conn *c, uint64_t now_us) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->now_us = now_us;
    for (int i = 0; i < WB_QUIC_SP_COUNT; i++)
        wb_quic_space_init(&c->sp[i], now_us);
}

void wb_quic_conn_tick(wb_quic_conn *c, uint64_t now_us) {
    if (!c) return;
    c->now_us = now_us;
    for (int i = 0; i < WB_QUIC_SP_COUNT; i++)
        wb_quic_space_tick(&c->sp[i], now_us);
}

void wb_quic_conn_install_keys(wb_quic_conn *c, wb_quic_sp sp,
                               const uint8_t *secret, const uint8_t *key,
                               const uint8_t *iv, const uint8_t *hp) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return;
    wb_quic_key_set *k = &c->keys[sp];
    memset(k, 0, sizeof *k);
    if (secret) memcpy(k->secret, secret, WB_QUIC_SECRET_LEN);
    if (key)    memcpy(k->key, key, WB_QUIC_KEY_LEN);
    if (iv)     memcpy(k->iv, iv, WB_QUIC_IV_LEN);
    if (hp)     memcpy(k->hp, hp, WB_QUIC_HP_LEN);
    k->installed = 1;
}

int wb_quic_conn_has_keys(const wb_quic_conn *c, wb_quic_sp sp) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return 0;
    return c->keys[sp].installed;
}

const wb_quic_key_set *wb_quic_conn_keys(const wb_quic_conn *c, wb_quic_sp sp) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return NULL;
    return c->keys[sp].installed ? &c->keys[sp] : NULL;
}

void wb_quic_conn_discard_space(wb_quic_conn *c, wb_quic_sp sp) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return;
    /* RFC 9000 section 17.1 forbids reusing a packet number, so the sequence
     * continues. Everything else about the space is abandoned. */
    uint64_t next = c->sp[sp].next_pn;
    memset(&c->keys[sp], 0, sizeof c->keys[sp]);
    wb_quic_space_init(&c->sp[sp], c->now_us);
    c->sp[sp].next_pn = next;
}

uint64_t wb_quic_conn_send(wb_quic_conn *c, wb_quic_sp sp, uint32_t bytes,
                           int ack_eliciting, int padding) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return 0;
    return wb_quic_space_send(&c->sp[sp], bytes, ack_eliciting, padding);
}

int wb_quic_conn_acked(wb_quic_conn *c, wb_quic_sp sp, uint64_t pn) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return -1;
    return wb_quic_space_acked(&c->sp[sp], pn);
}

size_t wb_quic_conn_lost(wb_quic_conn *c, wb_quic_sp sp, uint64_t *pns,
                         size_t max, uint64_t *loss_time) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return 0;
    return wb_quic_space_lost(&c->sp[sp], pns, max, loss_time);
}

uint64_t wb_quic_conn_pto(wb_quic_conn *c, wb_quic_sp sp,
                          uint64_t max_ack_delay_us, int confirmed, int backoff) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return WB_QUIC_GRANULARITY_US;
    return wb_quic_space_pto(&c->sp[sp], max_ack_delay_us, confirmed, backoff);
}

int wb_quic_conn_pn_exhausted(const wb_quic_conn *c, wb_quic_sp sp) {
    if (!c || sp < 0 || sp >= WB_QUIC_SP_COUNT) return 0;
    return wb_quic_space_pn_exhausted(&c->sp[sp]);
}
