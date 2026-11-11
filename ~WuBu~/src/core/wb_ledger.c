/*
 * wb_ledger.c -- the creator economy ledger (LP-55 stone 1). See wb_ledger.h.
 */
#include "wb_ledger.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* FNV-1a, chained entry to entry so any edit to an earlier entry changes every
 * value after it. */
static uint64_t wb_ledger_mix(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
    return h;
}

static uint64_t wb_ledger_hash(const wb_ledger_entry *e) {
    uint64_t h = 1469598103934665603ULL;
    h = wb_ledger_mix(h, e->id);
    h = wb_ledger_mix(h, (uint64_t)e->account);
    h = wb_ledger_mix(h, (uint64_t)e->amount);
    h = wb_ledger_mix(h, (uint64_t)e->reason);
    h = wb_ledger_mix(h, e->tick);
    h = wb_ledger_mix(h, e->prev);
    return h;
}

int wb_ledger_init(wb_ledger *l, uint32_t cap) {
    if (!l || cap == 0) return -1;
    l->entries = (wb_ledger_entry *)calloc(cap, sizeof(wb_ledger_entry));
    if (!l->entries) return -1;
    l->cap = cap;
    l->n = 0;
    l->next_id = 1;
    l->chain = 1469598103934665603ULL;
    return 0;
}

void wb_ledger_free(wb_ledger *l) {
    if (!l) return;
    free(l->entries);
    l->entries = NULL;
    l->cap = 0;
    l->n = 0;
}

void wb_ledger_clear(wb_ledger *l) {
    if (!l) return;
    l->n = 0;
    l->next_id = 1;
    l->chain = 1469598103934665603ULL;
}

uint64_t wb_ledger_post(wb_ledger *l, uint32_t account, int64_t amount,
                        uint32_t reason, uint64_t tick) {
    if (!l || !l->entries) return 0;
    if (amount == 0) return 0;                 /* a zero move is a no-op, not a record */
    if (l->n >= l->cap) return 0;

    wb_ledger_entry *e = &l->entries[l->n];
    e->id = l->next_id++;
    e->account = account;
    e->amount = amount;
    e->reason = reason;
    e->tick = tick;
    e->prev = l->chain;
    l->chain = wb_ledger_mix(l->chain, wb_ledger_hash(e));
    l->n++;
    return e->id;
}

uint64_t wb_ledger_transfer(wb_ledger *l, uint32_t from, uint32_t to, int64_t amount,
                            uint32_t reason, uint64_t tick) {
    if (!l || !l->entries) return 0;
    if (amount <= 0) return 0;                 /* a transfer moves a positive quantity */
    if (from == to) return 0;                  /* moving to yourself is not a move */
    if (l->n + 2 > l->cap) return 0;

    uint64_t debit = wb_ledger_post(l, from, -amount, reason, tick);
    if (debit == 0) return 0;
    uint64_t credit = wb_ledger_post(l, to, amount, reason, tick);
    if (credit == 0) return 0;                 /* unreachable: the capacity was checked */
    return credit;
}

int64_t wb_ledger_balance(const wb_ledger *l, uint32_t account) {
    if (!l || !l->entries) return 0;
    int64_t sum = 0;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->entries[i].account == account) sum += l->entries[i].amount;
    return sum;
}

int64_t wb_ledger_total(const wb_ledger *l) {
    if (!l || !l->entries) return 0;
    int64_t sum = 0;
    for (uint32_t i = 0; i < l->n; i++) sum += l->entries[i].amount;
    return sum;
}

uint64_t wb_ledger_chain(const wb_ledger *l) {
    return l ? l->chain : 0;
}

int wb_ledger_verify(const wb_ledger *l) {
    if (!l || !l->entries) return 0;
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < l->n; i++) {
        const wb_ledger_entry *e = &l->entries[i];
        if (e->prev != h) return 0;            /* an entry was inserted, removed or edited */
        h = wb_ledger_mix(h, wb_ledger_hash(e));
    }
    return h == l->chain;
}

int wb_ledger_has_account(const wb_ledger *l, uint32_t account) {
    if (!l || !l->entries) return 0;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->entries[i].account == account) return 1;
    return 0;
}
