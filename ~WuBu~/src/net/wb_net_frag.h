/*
 * wb_net_frag.h -- LP-94: packet fragmentation, reassembly, and the MTU/PSN
 * discipline that makes them necessary.
 *
 * A burst delta snapshot can be larger than the wire will carry. Before this
 * layer, that meant the whole packet was dropped. Now it is split into
 * fragments, each under the MTU, and reassembled at the far end.
 *
 * The cost is loss amplification: a message of n fragments survives only if
 * all n arrive, so its delivery probability is p^n where p is the per-packet
 * delivery rate. That is why the MTU is a budget and not a suggestion -- every
 * fragment is another chance to lose the message.
 *
 * Wire format. One leading type byte, so a fragment is a packet type of its
 * own rather than a flag on some other class:
 *
 *   0x00                          whole datagram, payload follows
 *   0x01 PSN:u16 IDX:u16 CNT:u16   one fragment of CNT, this is IDX
 *
 * PSN is 16-bit and wraps. Ordering uses the modular comparison, so a PSN
 * from before the wrap is not mistaken for one from after it.
 *
 * C11, no external deps.
 */
#ifndef WB_NET_FRAG_H
#define WB_NET_FRAG_H

#include "wb_net_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fiedler's numbers: 1472 is the upper MTU, 1200 the conservative one. */
#define WB_NET_MTU_UPPER       1472u
#define WB_NET_MTU_CONSERVATIVE 1200u

/* Largest number of fragments one message may be split into. With the 7-byte
   header this bounds a message at cap x (mtu - 7) bytes. */
#define WB_NET_FRAG_MAX_FRAGS  1216

typedef struct wb_net_frag wb_net_frag;

/* Wrap `inner`. `mtu` is clamped into [header+1, 1472]; pass 0 for the
 * conservative 1200. Returns NULL only on allocation failure. */
wb_net_frag *wb_net_frag_new(wb_net_transport *inner, uint16_t mtu);
void wb_net_frag_free(wb_net_frag *f);

/* The MTU in force, after clamping. */
uint16_t wb_net_frag_mtu(const wb_net_frag *f);

/* Queue a datagram for `peer`. 0 on success. -1 if the peer is bad, the
 * datagram cannot be reassembled within the datagram budget, or the MTU is too
 * small to carry even one fragment. Note a datagram the inner transport would
 * accept may still be refused here: the type byte and, for a fragmented one,
 * the fragment header both spend from the same buffer. */
int wb_net_frag_send(wb_net_frag *f, int peer, const wb_net_datagram *d);

/* Deliver every reassembled datagram that has become complete, in arrival
 * order. A message with a missing fragment is not delivered at all -- the
 * caller sees either the whole datagram or nothing. */
int wb_net_frag_poll(wb_net_frag *f, wb_net_recv_fn cb, void *ud);

/* Advance the inner transport's clock. */
void wb_net_frag_tick(wb_net_frag *f, double dt);

/* Feed one wire datagram directly. A relay, or a replay of a recorded one,
 * has bytes rather than a transport to poll. Returns 1 if the datagram
 * completed a message, in which case `out` holds it. */
int wb_net_frag_feed(wb_net_frag *f, int peer, const uint8_t *data,
                     uint16_t len, wb_net_datagram *out);

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_FRAG_H */
