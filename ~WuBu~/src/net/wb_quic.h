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

/* Parse an Initial. Returns 0 on success. `buf` must be a packet whose
 * protection has been removed: the Length field counts the AEAD tag, so
 * the payload is read as Length - PacketNumber - 16. */
int wb_quic_parse_initial(const uint8_t *buf, size_t len, uint32_t version,
                          wb_quic_hdr *hdr, wb_quic_initial *in);

/* ---- frames (RFC 9000 section 19) ---- */

/* Frame types. The STREAM range 0x08..0x0f carries four flag bits. */
enum {
    WB_QUIC_FRAME_PADDING          = 0x00,
    WB_QUIC_FRAME_PING             = 0x01,
    WB_QUIC_FRAME_ACK              = 0x02,   /* 0x03 adds the ECN counts */
    WB_QUIC_FRAME_RESET_STREAM     = 0x04,
    WB_QUIC_FRAME_STOP_SENDING     = 0x05,
    WB_QUIC_FRAME_CRYPTO           = 0x06,
    WB_QUIC_FRAME_NEW_TOKEN        = 0x07,
    WB_QUIC_FRAME_STREAM_BASE      = 0x08,
    WB_QUIC_FRAME_MAX_DATA         = 0x10,
    WB_QUIC_FRAME_MAX_STREAM_DATA  = 0x11,
    WB_QUIC_FRAME_MAX_STREAMS      = 0x12,   /* 0x13 is unidirectional */
    WB_QUIC_FRAME_DATA_BLOCKED     = 0x14,
    WB_QUIC_FRAME_STREAM_DATA_BLOCKED = 0x15,
    WB_QUIC_FRAME_STREAMS_BLOCKED  = 0x16,   /* 0x17 is unidirectional */
    WB_QUIC_FRAME_NEW_CONNECTION_ID = 0x18,
    WB_QUIC_FRAME_RETIRE_CONNECTION_ID = 0x19,
    WB_QUIC_FRAME_PATH_CHALLENGE   = 0x1a,
    WB_QUIC_FRAME_PATH_RESPONSE    = 0x1b,
    WB_QUIC_FRAME_CONNECTION_CLOSE = 0x1c,   /* 0x1d is application error */
    WB_QUIC_FRAME_HANDSHAKE_DONE   = 0x1e
};

/* The STREAM flags, so a type can be built and read without remembering which
 * bit is which: OFF, LEN, FIN. */
enum { WB_QUIC_STREAM_OFF = 0x04, WB_QUIC_STREAM_LEN = 0x02, WB_QUIC_STREAM_FIN = 0x01 };

#define WB_QUIC_ACK_MAX_RANGES 32

typedef struct {
    uint64_t largest;
    uint64_t delay;
    int      range_count;
    uint64_t first_range;
    uint64_t gap[WB_QUIC_ACK_MAX_RANGES];
    uint64_t range_len[WB_QUIC_ACK_MAX_RANGES];
    int      has_ecn;
    uint64_t ect0, ect1, ce;
} wb_quic_ack;

typedef struct {
    uint64_t type;
    const uint8_t *body;
    size_t   body_len;
} wb_quic_frame;

/* Walk a packet payload frame by frame. `off` is carried across calls, starting
 * at 0. Returns 1 for a frame, 0 when the payload is exhausted, -1 if a frame
 * is truncated or its type is not a valid varint. */
int wb_quic_frame_next(const uint8_t *buf, size_t len, size_t *off,
                       wb_quic_frame *f);

/* Builders. Each returns the byte count, or 0 if it does not fit. */
size_t wb_quic_frame_ping(uint8_t *out, size_t cap);
size_t wb_quic_frame_padding(uint8_t *out, size_t cap, size_t n);
size_t wb_quic_frame_handshake_done(uint8_t *out, size_t cap);

/* Build an ACK from a sorted list of acknowledged packet numbers. `ecn` adds the
 * ECN counts (type 0x03) and then requires three counts. */
size_t wb_quic_frame_ack(uint8_t *out, size_t cap, int ecn,
                         uint64_t largest, uint64_t delay,
                         const uint64_t *acked, size_t n_acked,
                         uint64_t ect0, uint64_t ect1, uint64_t ce);

size_t wb_quic_frame_crypto(uint8_t *out, size_t cap, uint64_t offset,
                            const void *data, size_t len);
size_t wb_quic_frame_stream(uint8_t *out, size_t cap, uint64_t stream_id,
                            uint64_t offset, const void *data, size_t len,
                            int fin);
size_t wb_quic_frame_new_token(uint8_t *out, size_t cap, const void *token,
                               size_t len);
size_t wb_quic_frame_reset_stream(uint8_t *out, size_t cap, uint64_t stream_id,
                                  uint64_t error, uint64_t final_size);
size_t wb_quic_frame_stop_sending(uint8_t *out, size_t cap, uint64_t stream_id,
                                  uint64_t error);
size_t wb_quic_frame_max_data(uint8_t *out, size_t cap, uint64_t maximum);
size_t wb_quic_frame_max_stream_data(uint8_t *out, size_t cap,
                                     uint64_t stream_id, uint64_t maximum);
size_t wb_quic_frame_max_streams(uint8_t *out, size_t cap, int uni,
                                 uint64_t maximum);
size_t wb_quic_frame_data_blocked(uint8_t *out, size_t cap, uint64_t limit);
size_t wb_quic_frame_stream_data_blocked(uint8_t *out, size_t cap,
                                         uint64_t stream_id, uint64_t limit);
size_t wb_quic_frame_streams_blocked(uint8_t *out, size_t cap, int uni,
                                     uint64_t limit);
size_t wb_quic_frame_new_connection_id(uint8_t *out, size_t cap,
                                       uint64_t seq, uint64_t retire_prior_to,
                                       const uint8_t *cid, size_t cid_len,
                                       const uint8_t token[16]);
size_t wb_quic_frame_retire_connection_id(uint8_t *out, size_t cap,
                                          uint64_t seq);
size_t wb_quic_frame_path_challenge(uint8_t *out, size_t cap,
                                    const uint8_t data[8]);
size_t wb_quic_frame_path_response(uint8_t *out, size_t cap,
                                   const uint8_t data[8]);
size_t wb_quic_frame_connection_close(uint8_t *out, size_t cap, int application,
                                      uint64_t error, uint64_t frame_type,
                                      const char *reason);

/* Parsers. Each returns 0 on success, -1 on a malformed body. */
int wb_quic_parse_ack(const uint8_t *body, size_t len, int type,
                      wb_quic_ack *out);
int wb_quic_parse_crypto(const uint8_t *body, size_t len, uint64_t *offset,
                         const uint8_t **data, size_t *dlen);
int wb_quic_parse_stream(const uint8_t *body, size_t len, int flags,
                         uint64_t *stream_id, uint64_t *offset, int *has_len,
                         const uint8_t **data, size_t *dlen, int *fin);
int wb_quic_parse_new_connection_id(const uint8_t *body, size_t len,
                                    uint64_t *seq, uint64_t *retire_prior_to,
                                    uint8_t *cid, size_t *cid_len,
                                    uint8_t token[16]);
int wb_quic_parse_path_data(const uint8_t *body, size_t len, uint8_t data[8]);
int wb_quic_parse_connection_close(const uint8_t *body, size_t len, int application,
                                   uint64_t *error, uint64_t *frame_type,
                                   const char **reason, size_t *reason_len);

/* How many acknowledged packet numbers an ACK covers, given its ranges. */

/* Expand an ACK into its acknowledged packet numbers, descending. Stops at
 * `max` entries. Returns the count written. */
size_t wb_quic_ack_expand(const wb_quic_ack *ack, uint64_t *out, size_t max);
uint64_t wb_quic_ack_count(const wb_quic_ack *ack);

#ifdef __cplusplus
}
#endif

#endif /* WB_QUIC_H */
