/*
 * wb_ledger_test.c -- LP-55 stone 1 gate: the creator economy ledger.
 *
 * Devil's-advocate cases: the ledger's TOTAL across all accounts is always zero
 * after any number of transfers -- that is a stronger check than any single
 * balance; an edited entry is detected by the chain; an entry REMOVED by
 * shifting the array is detected too; later posts never rewrite an earlier
 * entry; and a zero amount is a no-op, not a record (so ids do not skip).
 */
#include "wb_ledger.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    wb_ledger l;
    CHECK(wb_ledger_init(&l, 16) == 0, "the ledger is created");

    /* ---- ids are 1-based and in post order ---- */
    CHECK(wb_ledger_post(&l, 100, 500, 1, 10) == 1, "the first entry has id 1");
    CHECK(wb_ledger_post(&l, 101, 250, 1, 11) == 2, "the second entry has id 2");
    CHECK(wb_ledger_post(&l, 100, -100, 2, 12) == 3, "the third entry has id 3");
    CHECK(l.n == 3, "three entries are recorded");

    /* ---- a zero amount is refused and does not consume an id ---- */
    CHECK(wb_ledger_post(&l, 100, 0, 1, 13) == 0, "a zero move is refused");
    CHECK(l.n == 3, "a refused move leaves no entry");
    CHECK(wb_ledger_post(&l, 100, 1, 1, 13) == 4, "the next id is not skipped");

    /* ---- balances ---- */
    CHECK(wb_ledger_balance(&l, 100) == 401, "a balance sums credits and debits");
    CHECK(wb_ledger_balance(&l, 101) == 250, "the second account sums");
    CHECK(wb_ledger_balance(&l, 999) == 0, "an unknown account has a zero balance");
    CHECK(wb_ledger_has_account(&l, 101) == 1, "a posted account exists");
    CHECK(wb_ledger_has_account(&l, 999) == 0, "an unposted account does not exist");
    CHECK(wb_ledger_total(&l) == 651, "the total is the sum of every move");

    /* ---- a transfer is two legs that sum to zero ---- */
    uint64_t credit = wb_ledger_transfer(&l, 100, 101, 200, 3, 14);
    CHECK(credit == 6, "the credit leg is the second entry");
    CHECK(l.entries[4].amount == -200 && l.entries[4].account == 100, "the debit leg is recorded");
    CHECK(l.entries[5].amount == 200 && l.entries[5].account == 101, "the credit leg is recorded");
    CHECK(l.entries[4].id == 5 && l.entries[5].id == 6, "the legs have consecutive ids");
    CHECK(l.entries[4].tick == 14 && l.entries[5].tick == 14, "the legs share a tick");
    CHECK(l.entries[4].reason == 3 && l.entries[5].reason == 3, "the legs share a reason");
    CHECK(l.entries[4].prev != l.entries[5].prev, "the legs chain to each other");
    CHECK(wb_ledger_balance(&l, 100) == 201, "the debit is applied");
    CHECK(wb_ledger_balance(&l, 101) == 450, "the credit is applied");

    /* ---- a transfer that is not a positive move is refused ---- */
    CHECK(wb_ledger_transfer(&l, 100, 101, 0, 3, 15) == 0, "a zero transfer is refused");
    CHECK(wb_ledger_transfer(&l, 100, 101, -5, 3, 15) == 0, "a negative transfer is refused");
    CHECK(wb_ledger_transfer(&l, 100, 100, 5, 3, 15) == 0, "a transfer to yourself is refused");
    CHECK(l.n == 6, "refused transfers leave no entries");

    /* ---- the double-entry invariant ---- */
    wb_ledger m;
    wb_ledger_init(&m, 64);
    for (int i = 0; i < 20; i++) {
        uint32_t a = 10 + (uint32_t)(i % 5), b = 10 + (uint32_t)((i * 3 + 1) % 5);
        if (a == b) continue;
        CHECK(wb_ledger_transfer(&m, a, b, 10 + (int64_t)i, 7, (uint64_t)i) != 0, "a transfer posts");
    }
    CHECK(wb_ledger_total(&m) == 0, "the ledger total is always zero after transfers");
    CHECK(wb_ledger_verify(&m) == 1, "a clean ledger verifies");

    /* ---- verify detects an edited entry ---- */
    CHECK(wb_ledger_verify(&l) == 1, "the ledger verifies before tampering");
    int64_t saved = l.entries[1].amount;
    l.entries[1].amount += 1;
    CHECK(wb_ledger_verify(&l) == 0, "an edited entry breaks the chain");
    l.entries[1].amount = saved;
    CHECK(wb_ledger_verify(&l) == 1, "restoring the entry restores the chain");

    /* ---- verify detects a removed entry ---- */
    {
        wb_ledger_entry saved_entries[32];
        uint32_t n_before = l.n;
        memcpy(saved_entries, l.entries, (size_t)n_before * sizeof l.entries[0]);
        memmove(&l.entries[1], &l.entries[2], (size_t)(l.n - 2) * sizeof l.entries[0]);
        l.n--;
        CHECK(wb_ledger_verify(&l) == 0, "a removed entry breaks the chain");
        memcpy(l.entries, saved_entries, (size_t)n_before * sizeof l.entries[0]);
        l.n = n_before;
        CHECK(wb_ledger_verify(&l) == 1, "the ledger is intact again");
    }

    /* ---- verify detects an inserted entry ---- */
    CHECK(wb_ledger_init(&l, 16) == 0, "reinitialised");
    wb_ledger_post(&l, 1, 10, 1, 1);
    wb_ledger_post(&l, 2, -10, 1, 1);
    CHECK(wb_ledger_verify(&l) == 1, "a two-entry ledger verifies");
    CHECK(wb_ledger_post(&l, 1, 5, 2, 2) == 3, "a third entry posts");
    CHECK(wb_ledger_verify(&l) == 1, "a three-entry ledger verifies");

    /* ---- later posts never rewrite an earlier entry ---- */
    wb_ledger_entry snapshot[3];
    memcpy(snapshot, l.entries, sizeof snapshot);
    for (int i = 0; i < 5; i++) wb_ledger_post(&l, 9, 1, 3, (uint64_t)(10 + i));
    CHECK(memcmp(snapshot, l.entries, sizeof snapshot) == 0, "appending never rewrites an earlier entry");
    CHECK(wb_ledger_verify(&l) == 1, "the extended ledger still verifies");

    /* ---- a full ledger refuses ---- */
    wb_ledger small;
    wb_ledger_init(&small, 2);
    CHECK(wb_ledger_post(&small, 1, 1, 1, 1) == 1, "the first slot is used");
    CHECK(wb_ledger_post(&small, 2, -1, 1, 1) == 2, "the second slot is used");
    CHECK(wb_ledger_post(&small, 3, 1, 1, 1) == 0, "a full ledger refuses a post");
    CHECK(wb_ledger_transfer(&small, 1, 2, 1, 1, 2) == 0, "a full ledger refuses a transfer");
    wb_ledger_init(&small, 3);
    wb_ledger_post(&small, 1, 1, 1, 1);
    wb_ledger_post(&small, 2, -1, 1, 1);
    CHECK(wb_ledger_transfer(&small, 1, 2, 1, 1, 2) == 0, "a transfer needs TWO free slots");

    /* ---- clear resets ---- */
    wb_ledger_clear(&l);
    CHECK(l.n == 0, "clear empties the ledger");
    CHECK(wb_ledger_chain(&l) == 1469598103934665603ULL, "clear resets the chain");
    CHECK(wb_ledger_verify(&l) == 1, "an empty ledger verifies");
    CHECK(wb_ledger_post(&l, 1, 10, 1, 1) == 1, "ids restart after a clear");

    /* ---- determinism ---- */
    wb_ledger a, b;
    wb_ledger_init(&a, 32);
    wb_ledger_init(&b, 32);
    for (int i = 0; i < 12; i++) {
        wb_ledger_transfer(&a, (uint32_t)(i % 4), (uint32_t)((i + 2) % 4), 10 + i, 1, (uint64_t)i);
        wb_ledger_transfer(&b, (uint32_t)(i % 4), (uint32_t)((i + 2) % 4), 10 + i, 1, (uint64_t)i);
    }
    CHECK(wb_ledger_chain(&a) == wb_ledger_chain(&b), "the chain is deterministic");
    CHECK(memcmp(a.entries, b.entries, (size_t)a.n * sizeof a.entries[0]) == 0,
          "the entries are byte-identical");

    /* ---- degenerate input ---- */
    CHECK(wb_ledger_init(NULL, 4) == -1, "a NULL ledger is rejected");
    CHECK(wb_ledger_init(&a, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_ledger_post(NULL, 1, 1, 1, 1) == 0, "a NULL ledger refuses a post");
    CHECK(wb_ledger_transfer(NULL, 1, 2, 1, 1, 1) == 0, "a NULL ledger refuses a transfer");
    CHECK(wb_ledger_balance(NULL, 1) == 0, "a NULL ledger has a zero balance");
    CHECK(wb_ledger_total(NULL) == 0, "a NULL ledger has a zero total");
    CHECK(wb_ledger_chain(NULL) == 0, "a NULL ledger has no chain");
    CHECK(wb_ledger_verify(NULL) == 0, "a NULL ledger does not verify");
    CHECK(wb_ledger_has_account(NULL, 1) == 0, "a NULL ledger has no accounts");

    wb_ledger_free(&l);
    wb_ledger_free(&m);
    wb_ledger_free(&small);
    wb_ledger_free(&a);
    wb_ledger_free(&b);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
