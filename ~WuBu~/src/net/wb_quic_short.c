/*
 * wb_quic_short.c -- LP-93 stone 8: the short header and 1-RTT.
 */
#include "wb_quic_short.h"

#include "wb_hkdf.h"

#include <string.h>

int wb_quic_keys_update(wb_quic_keys *k) {
    if (!k) return -1;

    /* RFC 9001 section 6.1: secret_<n+1> = HKDF-Expand-Label(secret_<n>,
     * "quic ku", "", Hash.length), then the key and IV from that secret as
     * section 5.1 says. The header protection key is deliberately NOT
     * updated. */
    uint8_t secret[WB_QUIC_SECRET_LEN];
    if (wb_hkdf_expand_label(k->secret, WB_QUIC_SECRET_LEN, "quic ku",
                             NULL, 0, secret, WB_QUIC_SECRET_LEN) != 0)
        return -1;
    memcpy(k->secret, secret, WB_QUIC_SECRET_LEN);

    if (wb_hkdf_expand_label(k->secret, WB_QUIC_SECRET_LEN, "quic key",
                             NULL, 0, k->key, WB_QUIC_KEY_LEN) != 0)
        return -1;
    if (wb_hkdf_expand_label(k->secret, WB_QUIC_SECRET_LEN, "quic iv",
                             NULL, 0, k->iv, WB_QUIC_IV_LEN) != 0)
        return -1;

    k->key_phase ^= 1;
    return 0;
}

int wb_quic_short_build(uint8_t *out, size_t *len, size_t cap,
                        const wb_quic_keys *k, int spin, int key_phase,
                        const uint8_t *dcid, size_t dcid_len,
                        uint64_t pn, int pn_len,
                        const uint8_t *payload, size_t payload_len) {
    if (!out || !len || !k) return -1;
    if (pn_len < 1 || pn_len > 4) return -1;
    if (dcid_len > 20) return -1;
    if (pn >= (UINT64_C(1) << (8 * pn_len))) return -1;

    size_t n = 1u + dcid_len + (size_t)pn_len + payload_len;
    if (cap < n + WB_QUIC_GCM_TAG_LEN) return -1;

    /* RFC 9000 section 17.3: header form 0, fixed bit 1, spin, two reserved
     * bits that MUST be 0, the key phase, then the packet number length. */
    out[0] = (uint8_t)(0x40u |
                       (spin ? 0x20u : 0u) |
                       (key_phase ? 0x04u : 0u) |
                       (uint8_t)(pn_len - 1));
    if (dcid_len) memcpy(out + 1, dcid, dcid_len);
    size_t pn_offset = 1 + dcid_len;
    for (int i = 0; i < pn_len; i++)
        out[pn_offset + (size_t)(pn_len - 1 - i)] = (uint8_t)(pn >> (8 * i));
    size_t p = pn_offset + (size_t)pn_len;
    if (payload_len) {
        memcpy(out + p, payload, payload_len);
        p += payload_len;
    }
    *len = p;

    return wb_quic_protect(k, out, len, cap, pn_offset, (size_t)pn_len, pn, 0);
}

int wb_quic_short_open(uint8_t *pkt, size_t *len,
                       uint8_t *scratch, size_t scratch_cap,
                       const wb_quic_keys *keys, int key_count,
                       const uint8_t *dcid, size_t dcid_len,
                       int *key_phase, uint64_t *pn, size_t *payload_off) {
    if (!pkt || !len || !keys || key_count <= 0) return -1;
    if (scratch_cap < *len) return -1;
    if (*len < 2) return -1;
    /* The header form bit is not protected: a short header starts with 0. */
    if (pkt[0] & 0x80u) return -1;
    /* RFC 9000 section 17.3: the fixed bit must be 1 or the packet is not
     * valid in this version and MUST be discarded. */
    if (!(pkt[0] & 0x40u)) return -1;

    size_t pn_offset = 1 + dcid_len;
    if (*len < pn_offset + 16) return -1;
    /* A packet for another connection must not be processed. `dcid` may be
     * NULL when the caller is not ready to check. */
    if (dcid && (dcid_len == 0 || memcmp(pkt + 1, dcid, dcid_len) != 0))
        return -1;

    for (int ki = 0; ki < key_count; ki++) {
        for (int pnl = 1; pnl <= 4; pnl++) {
            memcpy(scratch, pkt, *len);
            size_t cl = *len;
            if (wb_quic_unprotect(&keys[ki], scratch, &cl, pn_offset,
                                  (size_t)pnl, 0) != 0)
                continue;
            uint8_t b0 = scratch[0];
            /* RFC 9000 section 17.3: the reserved bits are protected and a
             * non-zero value after removing both protections is a
             * PROTOCOL_VIOLATION. Checked only after the tag verifies, because
             * checking them on the protected header is an attack. */
            if ((b0 & 0x18u) != 0) continue;
            if (((b0 & 0x03u) + 1u) != (size_t)pnl) continue;
            if (((b0 >> 2) & 1u) != (uint8_t)(keys[ki].key_phase & 1)) continue;
            memcpy(pkt, scratch, cl);
            *len = cl;
            uint64_t num = 0;
            for (size_t i = 0; i < (size_t)pnl; i++)
                num = (num << 8) | pkt[pn_offset + i];
            if (key_phase) *key_phase = keys[ki].key_phase;
            if (pn) *pn = num;
            if (payload_off) *payload_off = pn_offset + (size_t)pnl;
            return 0;
        }
    }
    return -1;
}
