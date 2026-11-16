/*
 * wb_quic_protect.h -- LP-93 stone 3: QUIC packet protection (RFC 9001).
 *
 * The header codec and the frame codec describe a packet's shape; this makes
 * the packet private and authentic. Two pieces:
 *
 *   - The keys. Initial keys come from a salt and the client's Destination
 *     Connection ID (section 5.2), then each direction expands three labels:
 *     "quic key", "quic iv", "quic hp". Everything goes through
 *     HKDF-Expand-Label, which is TLS 1.3's labelling and not plain HKDF.
 *
 *   - The protection. The payload is sealed with AES-128-GCM, the nonce being
 *     the IV XOR the packet number, and the AAD being the header as it stands
 *     before header protection. Then a five-byte mask, sampled from the
 *     ciphertext and keyed with the hp key, is applied to the low bits of the
 *     first byte and to the packet number itself (section 5.4). An observer
 *     can still read the version and the connection IDs, which is what makes
 *     the protocol routable, but not the packet number.
 *
 * Only AES-128-GCM is wired here, which is what Initial packets use and what
 * RFC 9001's sample packets exercise.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_PROTECT_H
#define WB_QUIC_PROTECT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_QUIC_INITIAL_SALT_LEN 20
#define WB_QUIC_KEY_LEN      16
#define WB_QUIC_IV_LEN       12
#define WB_QUIC_HP_LEN       16
#define WB_QUIC_SECRET_LEN   32
#define WB_QUIC_SAMPLE_LEN   16
#define WB_QUIC_MASK_LEN      5

/* The AEAD tag every long packet carries; the Initial Length field counts it. */
#define WB_QUIC_GCM_TAG_LEN  16

typedef struct {
    uint8_t secret[WB_QUIC_SECRET_LEN];
    uint8_t key[WB_QUIC_KEY_LEN];
    uint8_t iv[WB_QUIC_IV_LEN];
    uint8_t hp[WB_QUIC_HP_LEN];
    /* Which key phase this set is (RFC 9001 section 6). Not protected; a
     * receiver picks the set by trying. */
    int key_phase;
} wb_quic_keys;

/* The v1 Initial salt, from RFC 9001 section 5.2. Future versions pick a
 * different salt precisely so that a middlebox cannot replay old keys. */
extern const uint8_t wb_quic_initial_salt_v1[WB_QUIC_INITIAL_SALT_LEN];

/* Derive both directions' Initial keys from the client's Destination
 * Connection ID (RFC 9001 section 5.2). `client` protects packets the client
 * sends and `server` the ones the server sends. */
void wb_quic_initial_keys(const uint8_t *dcid, size_t dcid_len,
                          wb_quic_keys *client, wb_quic_keys *server);

/* The header-protection mask: AES-ECB(hp, sample)[0..5] (section 5.4.2). */
void wb_quic_hp_mask(const uint8_t hp[WB_QUIC_HP_LEN],
                     const uint8_t sample[WB_QUIC_SAMPLE_LEN],
                     uint8_t mask[WB_QUIC_MASK_LEN]);

/* Apply (or remove -- it is the same XOR) header protection in place.
 * `hdr` is the whole header, `pn_offset` where the packet number starts, and
 * `pn_len` how many bytes it occupies, at most 4 because the mask is 5 bytes.
 * A long header loses only the low four bits of its first byte; a short
 * header loses five. */
void wb_quic_hp_apply(uint8_t *hdr, size_t pn_offset, size_t pn_len,
                      const uint8_t mask[WB_QUIC_MASK_LEN], int long_header);

/* Seal a packet payload in place and protect its header.
 *
 * `pkt` holds the whole packet: the unprotected header (of length `hdr_len`,
 * which starts at `pn_offset` + `pn_len`) followed by the payload. On success
 * the buffer holds the protected packet and `*len` is its new length, which is
 * the old length plus the 16-byte tag. `cap` is the buffer's capacity.
 *
 * `long_header` selects how many bits of the first byte the mask covers: four
 * for a long header, five for a short one (RFC 9000 section 17.3).
 *
 * Returns 0, or -1 if the buffer is too small, the packet number does not fit
 * in `pn_len`, or the keys are unusable. */
int wb_quic_protect(const wb_quic_keys *k,
                    uint8_t *pkt, size_t *len, size_t cap,
                    size_t pn_offset, size_t pn_len, uint64_t pn,
                    int long_header);

/* The reverse. `pkt` holds the protected packet of `*len` bytes; on success it
 * holds the unprotected packet (header restored, payload in the clear) and
 * `*len` is its new length. Returns -1 if the tag does not verify, in which
 * case `pkt` is left untouched. */
int wb_quic_unprotect(const wb_quic_keys *k,
                      uint8_t *pkt, size_t *len,
                      size_t pn_offset, size_t pn_len, int long_header);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_PROTECT_H */
