/*
 * wb_quic_protect.c -- LP-93 stone 3: QUIC packet protection (RFC 9001).
 */
#include "wb_quic_protect.h"

#include "wb_aes_gcm.h"
#include "wb_hkdf.h"

#include <stdlib.h>
#include <string.h>

const uint8_t wb_quic_initial_salt_v1[WB_QUIC_INITIAL_SALT_LEN] = {
    0x38,0x76,0x2c,0xf7,0xf5,0x59,0x34,0xb3,0x4d,0x17,
    0x9a,0xe6,0xa4,0xc8,0x0c,0xad,0xcc,0xbb,0x7f,0x0a
};

static void derive_one(const uint8_t *initial_secret, const char *label,
                       wb_quic_keys *k) {
    if (wb_hkdf_expand_label(initial_secret, WB_QUIC_SECRET_LEN, label,
                            NULL, 0, k->secret, WB_QUIC_SECRET_LEN) != 0)
        return;
    wb_hkdf_expand_label(k->secret, WB_QUIC_SECRET_LEN, "quic key",
                         NULL, 0, k->key, WB_QUIC_KEY_LEN);
    wb_hkdf_expand_label(k->secret, WB_QUIC_SECRET_LEN, "quic iv",
                         NULL, 0, k->iv, WB_QUIC_IV_LEN);
    wb_hkdf_expand_label(k->secret, WB_QUIC_SECRET_LEN, "quic hp",
                         NULL, 0, k->hp, WB_QUIC_HP_LEN);
}

void wb_quic_initial_keys(const uint8_t *dcid, size_t dcid_len,
                          wb_quic_keys *client, wb_quic_keys *server) {
    uint8_t secret[WB_QUIC_SECRET_LEN];
    /* initial_secret = HKDF-Extract(initial_salt, client_dst_connection_id).
     * The salt is the key of the HMAC, the connection ID its message. */
    if (wb_hkdf_extract_sha256(wb_quic_initial_salt_v1, WB_QUIC_INITIAL_SALT_LEN,
                               dcid, dcid_len, secret) != 0)
        return;
    derive_one(secret, "client in", client);
    derive_one(secret, "server in", server);
}

void wb_quic_hp_mask(const uint8_t hp[WB_QUIC_HP_LEN],
                     const uint8_t sample[WB_QUIC_SAMPLE_LEN],
                     uint8_t mask[WB_QUIC_MASK_LEN]) {
    wb_aes128 *a = wb_aes128_new(hp);
    if (!a) {
        memset(mask, 0, WB_QUIC_MASK_LEN);
        return;
    }
    uint8_t out[16];
    wb_aes128_block(a, sample, out);
    memcpy(mask, out, WB_QUIC_MASK_LEN);
    wb_aes128_free(a);
}

void wb_quic_hp_apply(uint8_t *hdr, size_t pn_offset, size_t pn_len,
                      const uint8_t mask[WB_QUIC_MASK_LEN], int long_header) {
    if (!hdr || !mask || pn_len > WB_QUIC_MASK_LEN - 1) return;
    hdr[0] ^= (uint8_t)(mask[0] & (long_header ? 0x0f : 0x1f));
    for (size_t i = 0; i < pn_len; i++)
        hdr[pn_offset + i] ^= mask[i + 1];
}

/* The nonce is the IV with the packet number XORed into its last bytes,
 * big-endian and zero-padded (RFC 9001 section 5.3). */
static void packet_nonce(const uint8_t iv[WB_QUIC_IV_LEN], uint64_t pn,
                         uint8_t out[WB_QUIC_IV_LEN]) {
    memcpy(out, iv, WB_QUIC_IV_LEN);
    for (int i = 0; i < 8; i++)
        out[WB_QUIC_IV_LEN - 1 - i] ^= (uint8_t)(pn >> (8 * i));
}

int wb_quic_protect(const wb_quic_keys *k,
                    uint8_t *pkt, size_t *len, size_t cap,
                    size_t pn_offset, size_t pn_len, uint64_t pn) {
    if (!k || !pkt || !len || pn_len == 0 || pn_len > 4) return -1;
    if (*len < pn_offset + pn_len) return -1;
    if (cap < *len + 16) return -1;
    if (pn_len < 1 || pn >= (1ull << (8 * pn_len))) return -1;

    size_t hdr_len = pn_offset + pn_len;
    size_t plen = *len - hdr_len;

    /* the packet number goes into the header, big-endian, in as many bytes as
     * the header says it occupies */
    for (size_t i = 0; i < pn_len; i++)
        pkt[pn_offset + i] = (uint8_t)(pn >> (8 * (pn_len - 1 - i)));

    uint8_t nonce[WB_QUIC_IV_LEN];
    packet_nonce(k->iv, pn, nonce);

    /* Seal in place: the plaintext and ciphertext share the buffer, which is
     * safe because GCM writes forward and the tag goes past the old end. */
    wb_aes128 *a = wb_aes128_new(k->key);
    if (!a) return -1;
    uint8_t *payload = pkt + hdr_len;
    uint8_t tag[16];
    int rc = wb_aes128_gcm_encrypt(a, nonce, WB_QUIC_IV_LEN,
                                   pkt, hdr_len,        /* AAD: the header */
                                   payload, plen, payload, tag);
    wb_aes128_free(a);
    if (rc != 0) return -1;
    memcpy(payload + plen, tag, 16);

    /* RFC 9001 section 5.4.2: the sample is taken from an offset of four bytes
     * after the start of the packet number field -- "the Packet Number field is
     * assumed to be 4 bytes long (its maximum possible encoded length)" --
     * precisely because the receiver does not yet know the encoded length.
     * Sampling relative to `pn_len` would only be correct for four-byte
     * numbers. The packet must be long enough to hold the sample. */
    if (*len < pn_offset + 4 + WB_QUIC_SAMPLE_LEN) return -1;
    uint8_t mask[WB_QUIC_MASK_LEN];
    wb_quic_hp_mask(k->hp, pkt + pn_offset + 4, mask);
    /* A long header's first byte keeps its four high bits; a short header's
     * keeps three. The caller tells us which this is. */
    wb_quic_hp_apply(pkt, pn_offset, pn_len, mask, 1);

    *len += 16;
    return 0;
}

int wb_quic_unprotect(const wb_quic_keys *k,
                      uint8_t *pkt, size_t *len,
                      size_t pn_offset, size_t pn_len, int long_header) {
    if (!k || !pkt || !len || pn_len == 0 || pn_len > 4) return -1;
    if (*len < pn_offset + pn_len + 16) return -1;

    size_t hdr_len = pn_offset + pn_len;
    if (*len < hdr_len + 16) return -1;
    /* RFC 9001 section 5.4.2: four bytes after the packet number, always. */
    if (*len < pn_offset + 4 + WB_QUIC_SAMPLE_LEN) return -1;
    size_t clen = *len - hdr_len - 16;

    uint8_t mask[WB_QUIC_MASK_LEN];
    wb_quic_hp_mask(k->hp, pkt + pn_offset + 4, mask);
    wb_quic_hp_apply(pkt, pn_offset, pn_len, mask, long_header);

    /* the restored header carries the packet number, which is the nonce */
    uint64_t pn = 0;
    for (size_t i = 0; i < pn_len; i++) pn = (pn << 8) | pkt[pn_offset + i];
    uint8_t nonce[WB_QUIC_IV_LEN];
    packet_nonce(k->iv, pn, nonce);

    wb_aes128 *a = wb_aes128_new(k->key);
    if (!a) return -1;
    uint8_t *payload = pkt + hdr_len;
    int rc = wb_aes128_gcm_decrypt(a, nonce, WB_QUIC_IV_LEN,
                                   pkt, hdr_len,
                                   payload, clen, payload + clen, payload);
    wb_aes128_free(a);
    if (rc != 0) return -1;
    *len -= 16;
    return 0;
}
