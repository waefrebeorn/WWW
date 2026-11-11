/*
 * wb_ledger.h -- the creator economy ledger (LP-55 stone 1).
 *
 * A ledger is not a balance; it is a list of moves, and the balance is derived
 * from the list. That distinction is the whole design: a balance can be written
 * by a bug, but a ledger can only be extended, so any wrong balance can be
 * recomputed and any tampered entry is visible.
 *
 * Entries are APPEND-ONLY. Nothing here edits or removes an entry -- a wrong
 * entry is corrected by posting a reversing move, the same way a real
 * accountant does it. `wb_ledger_verify` recomputes the running chain from
 * entry zero and reports whether the history is intact.
 *
 * Double entry: a transfer posts two legs that sum to zero, so the ledger's
 * total across all accounts is ALWAYS zero. That invariant is what the gate
 * leans on -- it is a stronger check than any single balance.
 *
 * C11, no deps. Deterministic: entries are numbered in post order.
 */
#ifndef WB_LEDGER_H
#define WB_LEDGER_H

#include <stdint.h>

#define WB_LEDGER_MAX_REASON 64

typedef struct {
    uint64_t id;                          /* 1-based, in post order */
    uint32_t account;
    int64_t  amount;                      /* signed: positive is a credit */
    uint32_t reason;
    uint64_t tick;
    uint64_t prev;                        /* the chain value before this entry */
} wb_ledger_entry;

typedef struct {
    wb_ledger_entry *entries;
    uint32_t         cap;
    uint32_t         n;
    uint64_t         next_id;
    uint64_t         chain;               /* a running hash over every entry */
} wb_ledger;

int  wb_ledger_init(wb_ledger *l, uint32_t cap);
void wb_ledger_free(wb_ledger *l);
void wb_ledger_clear(wb_ledger *l);

/* Append one move. Returns the new entry's id, or 0 if refused (a zero amount,
 * a full ledger, or a NULL ledger). */
uint64_t wb_ledger_post(wb_ledger *l, uint32_t account, int64_t amount,
                        uint32_t reason, uint64_t tick);

/* Append a debit to `from` and a credit to `to` as ONE move. Returns the id of
 * the credit leg, or 0 if refused. */
uint64_t wb_ledger_transfer(wb_ledger *l, uint32_t from, uint32_t to, int64_t amount,
                            uint32_t reason, uint64_t tick);

int64_t  wb_ledger_balance(const wb_ledger *l, uint32_t account);
int64_t  wb_ledger_total(const wb_ledger *l);
uint64_t wb_ledger_chain(const wb_ledger *l);

/* 1 if every entry's `prev` links to its predecessor and the running chain
 * matches, i.e. the history has not been edited in place. */
int wb_ledger_verify(const wb_ledger *l);

/* 1 if the account exists in the ledger (it has at least one entry). */
int wb_ledger_has_account(const wb_ledger *l, uint32_t account);

#endif /* WB_LEDGER_H */
