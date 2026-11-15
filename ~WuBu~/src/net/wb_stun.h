/*
 * wb_stun.h -- LP-95 stone 1: STUN, the reflexive-address transaction.
 *
 * A host behind a NAT does not know its own public address; the NAT allocates
 * one per flow and the host cannot see it. STUN (RFC 8489) is the smallest
 * possible answer: send a Binding request to a server, and the server copies
 * the source address it saw back into the response. That address is the
 * server-reflexive candidate ICE gathers from.
 *
 * Wire format. A 20-byte header -- message type, message length, the magic
 * cookie 0x2112A442, and a 12-byte transaction ID -- followed by
 * type-length-value attributes, each padded to four bytes.
 *
 * XOR-MAPPED-ADDRESS (RFC 8489 section 14) obfuscates the reflexive address by
 * XOR'ing the port with the top 16 bits of the magic cookie and the address
 * with the cookie itself (IPv4) or the cookie followed by the transaction ID
 * (IPv6). The obfuscation is not security; it exists so a middlebox does not
 * mistake the address for something it can rewrite.
 *
 * C11, no external deps.
 */
#ifndef WB_STUN_H
#define WB_STUN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WB_STUN_MAGIC_COOKIE 0x2112A442u
#define WB_STUN_HEADER       20
#define WB_STUN_TXID_LEN     12

enum {
    WB_STUN_BINDING_REQUEST    = 0x0001,
    WB_STUN_BINDING_INDICATION = 0x0011,
    WB_STUN_BINDING_SUCCESS    = 0x0101,
    WB_STUN_BINDING_ERROR      = 0x0111
};

enum {
    WB_STUN_ATTR_MAPPED_ADDRESS     = 0x0001,
    WB_STUN_ATTR_XOR_MAPPED_ADDRESS = 0x0020,
    WB_STUN_ATTR_SOFTWARE           = 0x8022,
    WB_STUN_ATTR_FINGERPRINT        = 0x8028
};

/* A transport address. `family` is 4 or 6; `addr` holds 4 or 16 bytes. */
typedef struct {
    int      family;
    uint16_t port;
    uint8_t  addr[16];
} wb_stun_addr;

/* Build a Binding request. `txid` must be WB_STUN_TXID_LEN bytes. Returns the
 * message length, or 0 if it does not fit. */
size_t wb_stun_build_binding_request(uint8_t *out, size_t cap,
                                     const uint8_t txid[WB_STUN_TXID_LEN]);

/* Parse the header. Returns 0 on success, -1 if the buffer is not a STUN
 * message (short, wrong cookie, or a length that does not match). */
int wb_stun_parse_header(const uint8_t *buf, size_t len, uint16_t *type,
                         const uint8_t **txid);

/* Find the mapped address in a message. Prefers XOR-MAPPED-ADDRESS, falls back
 * to MAPPED-ADDRESS. Returns 0 on success, -1 if there is none. */
int wb_stun_find_mapped(const uint8_t *buf, size_t len, wb_stun_addr *out);

/* Build a Binding success response carrying `mapped`. Returns the length, or 0
 * if it does not fit. */
size_t wb_stun_build_binding_success(uint8_t *out, size_t cap,
                                     const uint8_t txid[WB_STUN_TXID_LEN],
                                     const wb_stun_addr *mapped);

/* Is this message type a Binding one? */
int wb_stun_is_binding(uint16_t type);

/* A server that answers Binding requests with the address it saw. This is the
 * whole of RFC 8489's server side for our purposes; it is also what the tests
 * run against, and what a real STUN server is a scaled-up version of. */
typedef struct wb_stun_server wb_stun_server;

wb_stun_server *wb_stun_server_new(void);
void wb_stun_server_free(wb_stun_server *s);

/* Handle one datagram. If it is a Binding request, a success response carrying
 * `observed` -- the address the request appeared to come from, which for UDP is
 * the source the transport received it from -- is written to `out` (capacity
 * `cap`) and handed to `send` for `peer`. Returns the response length, or 0 if
 * the datagram was not a Binding request. */
typedef int (*wb_stun_send_fn)(void *ud, int peer, const uint8_t *data, size_t len);

size_t wb_stun_server_handle(wb_stun_server *s, const uint8_t *buf, size_t len,
                             int peer, const wb_stun_addr *observed,
                             wb_stun_send_fn send, void *ud,
                             uint8_t *out, size_t cap);

/* How many requests this server has answered. */
unsigned long long wb_stun_server_requests(const wb_stun_server *s);

#ifdef __cplusplus
}
#endif

#endif /* WB_STUN_H */
