/*
 * wb_net_lockstep.h -- LP-87's last piece: the lockstep frame-stepper.
 *
 * Two peers agree on a tick, exchange that tick's inputs, and step. A peer
 * never steps ahead of the other: if the far input for tick T has not arrived,
 * tick T does not happen. That is the whole model -- it is what makes a desync
 * a DETECTABLE event rather than an invisible divergence, because both sides
 * are still talking about the same tick when the digests disagree.
 *
 * The stepper is transport-agnostic: it runs on a `wb_net_reliable` link (which
 * itself runs on the lossy pair or a UDP socket), and it never looks at the
 * simulation. The caller supplies a step callback and, after each step, the
 * state digest; the stepper compares the two peers' digests and reports the
 * first tick where they differ.
 *
 * Point-to-point: one link, two peers. That is the model the netcode needs.
 * C11, no external deps.
 */
#ifndef WB_NET_LOCKSTEP_H
#define WB_NET_LOCKSTEP_H

#include <stdint.h>

#include "wb_net.h"
#include "wb_net_reliable.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Remote state is kept this many ticks deep, so a peer may run ahead by up to
 * a few ticks before the ring wraps. */
#define WB_LOCKSTEP_WINDOW 128

typedef struct wb_net_lockstep wb_net_lockstep;

/* Called when a tick steps, with that tick's input from EVERY peer, indexed by
 * peer id (in[0] and in[1]). */
typedef void (*wb_lockstep_step_fn)(void *ud, uint32_t tick,
                                    const wb_net_input *in, int nin);

/* `local_peer` is 0 or 1; the far end is the other one. `net` is borrowed and
 * must outlive the stepper. */
wb_net_lockstep *wb_net_lockstep_new(int local_peer, float tick_rate,
                                     wb_net_reliable *net);
void wb_net_lockstep_free(wb_net_lockstep *ls);

/* Submit the local peer's input for a tick. The input is sent to the far peer
 * immediately, because lockstep cannot wait to be asked. Refused if the tick is
 * already stepped or outside the window. */
int wb_net_lockstep_submit(wb_net_lockstep *ls, uint32_t tick,
                           const wb_net_input *in);

/* Step one tick if every peer's input for it is present. Returns 1 if it
 * stepped, 0 if it is still waiting for the far peer. `fn` is called only
 * when it stepped. */
int wb_net_lockstep_step(wb_net_lockstep *ls, wb_lockstep_step_fn fn, void *ud);

/* Report this peer's state digest for a stepped tick. Digests are exchanged
 * out of band; the first tick where the two disagree is the desync. */
void wb_net_lockstep_digest(wb_net_lockstep *ls, uint32_t tick, uint64_t digest);

/* The first tick where the two peers' digests disagreed (0 if none yet). */
int      wb_net_lockstep_desynced(const wb_net_lockstep *ls, uint32_t *tick);

/* Deliver whatever the link has. Call once per tick, after ticking it. */
int  wb_net_lockstep_poll(wb_net_lockstep *ls);

uint32_t wb_net_lockstep_tick(const wb_net_lockstep *ls);      /* next to step */
float    wb_net_lockstep_dt(const wb_net_lockstep *ls);       /* fixed step */
uint32_t wb_net_lockstep_stalled(const wb_net_lockstep *ls);  /* waits so far */

#ifdef __cplusplus
}
#endif

#endif /* WB_NET_LOCKSTEP_H */
