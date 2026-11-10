/*
 * wb_handoff.h -- server meshing authority handoff (LP-33 stone 1).
 *
 * When an entity crosses a server boundary, authority has to move atomically:
 * the source freezes the entity, the target loads it, both agree, and only then
 * does the source release. Doing it in the wrong order either duplicates the
 * entity on two servers or loses it between them.
 *
 * This is that protocol as a state machine, plus the epoch fence that makes a
 * stale message from an earlier handoff harmless: every handoff bumps the epoch
 * and a message carrying an old epoch is dropped.
 *
 * C11, no deps.
 */
#ifndef WB_HANDOFF_H
#define WB_HANDOFF_H

#include <stdint.h>

typedef enum {
    WB_HANDOFF_IDLE = 0,     /* no handoff in progress */
    WB_HANDOFF_FREEZING,     /* the source has stopped simulating the entity */
    WB_HANDOFF_HANDSHAKE,    /* both sides are agreeing */
    WB_HANDOFF_ACCEPTED,     /* the target holds authority */
    WB_HANDOFF_REJECTED      /* the target refused; the source keeps authority */
} wb_handoff_state;

typedef struct {
    uint32_t          entity;
    uint32_t          source_server;
    uint32_t          target_server;
    wb_handoff_state  state;
    uint32_t          epoch;     /* bumps every handoff */
    uint32_t          attempts;
} wb_handoff;

void wb_handoff_init(wb_handoff *h);

/* IDLE -> FREEZING. Returns 0 on success, -1 if a handoff is already running. */
int  wb_handoff_begin(wb_handoff *h, uint32_t entity, uint32_t source, uint32_t target);
/* FREEZING -> HANDSHAKE (the freeze is complete and acknowledged). */
int  wb_handoff_freeze_done(wb_handoff *h);
/* HANDSHAKE -> ACCEPTED (the target loaded the entity and took authority). */
int  wb_handoff_accept(wb_handoff *h);
/* HANDSHAKE -> REJECTED (the target could not take it). */
int  wb_handoff_reject(wb_handoff *h);
/* ACCEPTED -> IDLE (the source released authority; the handoff is complete). */
int  wb_handoff_commit(wb_handoff *h);
/* REJECTED -> IDLE (the source resumed authority). */
int  wb_handoff_rollback(wb_handoff *h);

int  wb_handoff_is_stale(const wb_handoff *h, uint32_t message_epoch);

#endif /* WB_HANDOFF_H */
