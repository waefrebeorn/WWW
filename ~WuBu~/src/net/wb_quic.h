/*
 * wb_quic.h -- LP-93 stone 1: QUIC's wire primitives.
 *
 * RFC 9000 is a large protocol, but three pieces of it are needed before any of
 * the rest can be built and are self-contained enough to verify on their own:
 *
 *   - the variable-length integer (section 16), which every frame field uses;
 *   - the packet number codec (section 17.1 and appendix A), which trades bytes
 *     on the wire for a small ambiguity window the receiver resolves against
 *     the highest packet it has seen;
 *   - the long header (section 17.2) and, on top of it, the Initial packet with
 *     its token and length fields.
 *
 * Everything here is checked against the RFC's own worked examples rather than
 * against the code's idea of itself: the eight-byte varint 0xc2197c5eff14e88c is
 * 151288809941952652, 0x7bbd is 15293, and packet number 0x9b32 after a largest
 * of 0xa82f30ea decodes to 0xa82f9b32.
 *
 * C11, no external deps.
 */
#ifndef WB_QUIC_H
#define WB_QUIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- variable-length integers (RFC 9000 section 16) ---- */

/* Encode `v`. Writes 1, 2, 4 or 8 bytes and returns that count. */
size_t wb_quic_varint_encode(uint64_t v, uint8_t out[8]);

/* Decode one. Returns 0 on success and sets *used; -1 if the buffer is short or
 * the value does not fit in 62 bits. */
int wb_quic_varint_decode(const uint8_t *buf, size_t len, uint64_t *v,
                          size_t *used);

/* ---- packet numbers (RFC 9000 section 17.1, appendix A.2/A.3) ---- */

/* Choose the shortest encoding that leaves the receiver able to recover
 * `full_pn` and write it big-endian. `have_largest` selects the "nothing has
 * been acknowledged yet" case. Returns the byte count, 1..4. */
size_t wb_quic_pn_encode(uint64_t full_pn, int have_largest,
                         uint64_t largest_acked, uint8_t out[4]);

/* Recover the full packet number. `pn_nbits` is 8, 16, 24 or 32. */
uint64_t wb_quic_pn_decode(uint64_t largest_pn, uint64_t truncated,
                           int pn_nbits);

/* ---- long header (RFC 9000 section 17.2) ---- */

#define WB_QUIC_VERSION_1 0x00000001u

enum {
    WB_QUIC_INITIAL = 0,
    WB_QUIC_ZERO_RTT = 1,
    WB_QUIC_HANDSHAKE = 2,
    WB_QUIC_RETRY = 3
};

typedef struct {
    int      form;          /* 1 = long, 0 = short */
    int      type;          /* long packet type, 0..3 */
    uint32_t version;
    size_t   dcid_len;
    uint8_t  dcid[20];
    size_t   scid_len;
    uint8_t  scid[20];
    size_t   header_len;    /* bytes consumed by the fixed part */
} wb_quic_hdr;

/* Parse the fixed part of a long header. Returns 0 on success. A Version
 * Negotiation packet (version 0) is parsed too; its type is -1. */
int wb_quic_parse_long_header(const uint8_t *buf, size_t len, wb_quic_hdr *h);

/* Build the fixed part. `pn_len` is 1..4. Returns the byte count. */
size_t wb_quic_build_long_header(uint8_t *out, size_t cap, int type,
                                 uint32_t version,
                                 const uint8_t *dcid, size_t dcid_len,
                                 const uint8_t *scid, size_t scid_len,
                                 int pn_len);

/* ---- the Initial packet (RFC 9000 section 17.2.2) ---- */

typedef struct {
    const uint8_t *token;
    size_t         token_len;
    const uint8_t *payload;
    size_t         payload_len;
    uint64_t       packet_number;
} wb_quic_initial;

/* Build an Initial: token length, token, length (of packet number plus
 * payload), packet number, payload. Returns the byte count, or 0 if it does
 * not fit. */
size_t wb_quic_build_initial(uint8_t *out, size_t cap,
                             const uint8_t *dcid, size_t dcid_len,
                             const uint8_t *scid, size_t scid_len,
                             const wb_quic_initial *in);

/* Parse an Initial. Returns 0 on success. */
int wb_quic_parse_initial(const uint8_t *buf, size_t len, uint32_t version,
                          wb_quic_hdr *hdr, wb_quic_initial *in);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_H */
