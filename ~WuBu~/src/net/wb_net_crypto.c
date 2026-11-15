/*
 * wb_net_crypto.c -- the sealed channel (LP-70 stone 5).
 */
#include "wb_net_crypto.h"

#include "../core/wb_aes_gcm.h"
#include "../core/wb_hkdf.h"
#include "../core/wb_sha256.h"
#include "../core/wb_x25519.h"

#include <stdlib.h>
#include <string.h>

/* Bound the derived key to this protocol and version, so a key derived for
 * one purpose can never be reused for another. */
static const char WB_NET_CRYPTO_INFO[] = "WuBu net crypto v1";

struct wb_net_crypto {
    uint8_t priv[32];
    uint8_t pub[32];
    uint8_t nonce_prefix[8];
    wb_aes256 *aes;
    int ready;
};

wb_net_crypto *wb_net_crypto_new(const uint8_t priv[32], uint8_t pub_out[32]) {
    wb_net_crypto *c = (wb_net_crypto *)calloc(1, sizeof *c);
    if (!c) return NULL;
    memcpy(c->priv, priv, 32);
    wb_x25519_public(c->pub, priv);
    if (pub_out) memcpy(pub_out, c->pub, 32);
    return c;
}

void wb_net_crypto_free(wb_net_crypto *c) {
    if (!c) return;
    wb_aes256_free(c->aes);
    free(c);
}

void wb_net_crypto_public(const wb_net_crypto *c, uint8_t pub[32]) {
    memcpy(pub, c->pub, 32);
}

int wb_net_crypto_ready(const wb_net_crypto *c) { return c && c->ready; }

int wb_net_crypto_handshake(wb_net_crypto *c, const uint8_t peer_pub[32]) {
    if (!c || c->ready) return -1;

    uint8_t shared[32];
    if (wb_x25519_shared(shared, c->priv, peer_pub) != 0) {
        /* All-zero shared secret: the peer sent a low-order point. There is no
         * key to derive, so the peer is refused rather than connected. */
        return -1;
    }

    /* Salt = both public keys, in a canonical order so both sides compute the
     * same one. Public values, so a clear-channel exchange of them is fine. */
    uint8_t salt[64];
    if (memcmp(c->pub, peer_pub, 32) <= 0) {
        memcpy(salt, c->pub, 32);
        memcpy(salt + 32, peer_pub, 32);
    } else {
        memcpy(salt, peer_pub, 32);
        memcpy(salt + 32, c->pub, 32);
    }

    uint8_t key[32];
    if (wb_hkdf_sha256(salt, sizeof salt, shared, sizeof shared,
                       (const uint8_t *)WB_NET_CRYPTO_INFO,
                       sizeof WB_NET_CRYPTO_INFO - 1,
                       key, sizeof key) != 0) {
        return -1;
    }
    memcpy(c->nonce_prefix, salt, sizeof c->nonce_prefix);
    c->aes = wb_aes256_new(key);
    if (!c->aes) return -1;
    c->ready = 1;
    return 0;
}

static void crypto_nonce(wb_net_crypto *c, uint32_t seq, uint8_t nonce[12]) {
    memcpy(nonce, c->nonce_prefix, 8);
    nonce[8] = (uint8_t)(seq >> 24);
    nonce[9] = (uint8_t)(seq >> 16);
    nonce[10] = (uint8_t)(seq >> 8);
    nonce[11] = (uint8_t)(seq);
}

int wb_net_crypto_seal(wb_net_crypto *c, uint32_t seq,
                       const void *aad, size_t aad_len,
                       const void *pt, size_t pt_len,
                       uint8_t *ct, uint8_t tag[16]) {
    if (!c || !c->ready) return -1;
    uint8_t nonce[12];
    crypto_nonce(c, seq, nonce);
    return wb_aes_gcm_encrypt(c->aes, nonce, sizeof nonce,
                              (const uint8_t *)aad, aad_len,
                              (const uint8_t *)pt, pt_len, ct, tag);
}

int wb_net_crypto_open(wb_net_crypto *c, uint32_t seq,
                       const void *aad, size_t aad_len,
                       const void *ct, size_t ct_len,
                       const uint8_t tag[16], void *pt) {
    if (!c || !c->ready) return -1;
    uint8_t nonce[12];
    crypto_nonce(c, seq, nonce);
    return wb_aes_gcm_decrypt(c->aes, nonce, sizeof nonce,
                              (const uint8_t *)aad, aad_len,
                              (const uint8_t *)ct, ct_len, tag,
                              (uint8_t *)pt);
}
