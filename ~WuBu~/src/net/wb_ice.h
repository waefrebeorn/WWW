/*
 * wb_ice.h -- LP-95 stone 2: ICE, the checklist that turns candidates into a
 * connection.
 *
 * STUN tells a host its reflexive address. ICE decides which of the several
 * addresses two hosts have will actually carry the traffic, and it decides by
 * trying them: every local candidate is paired with every remote one, the pairs
 * are ordered by a priority that prefers the best pair either side could have,
 * and each pair gets a connectivity check.
 *
 * Candidate priority (RFC 8445 section 5.1.2.1):
 *
 *     priority = 2^24 * type_pref + 2^8 * local_pref + (256 - component)
 *
 * Type preference is 126 for host, 110 for peer-reflexive, 100 for
 * server-reflexive and 0 for relayed -- a direct path beats a translated one,
 * which beats a relayed one.
 *
 * Pair priority (RFC 8445 section 5.1.2.3 / appendix B.5):
 *
 *     pair priority = 2^32 * MIN(G,D) + 2 * MAX(G,D) + (G > D ? 1 : 0)
 *
 * where G is the controlling agent's candidate and D the controlled agent's.
 * The odd shape is deliberate: it sorts first by the worse of the two
 * priorities, so a pair is never used ahead of one whose weakest side is
 * better. That MAX/MIN property is the part worth testing.
 *
 * Foundations group candidates that would succeed or fail together -- two
 * candidates off the same base address share a foundation, so a failure on one
 * tells you something about the other.
 *
 * C11, no external deps.
 */
#ifndef WB_ICE_H
#define WB_ICE_H

#include "wb_stun.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    WB_ICE_HOST = 0,
    WB_ICE_SERVER_REFLEXIVE = 1,
    WB_ICE_PEER_REFLEXIVE = 2,
    WB_ICE_RELAYED = 3
};

/* RFC 8445 section 5.1.2.2 recommended type preferences. */
#define WB_ICE_TYPE_PREF_HOST             126
#define WB_ICE_TYPE_PREF_PEER_REFLEXIVE   110
#define WB_ICE_TYPE_PREF_SERVER_REFLEXIVE 100
#define WB_ICE_TYPE_PREF_RELAYED            0

enum {
    WB_ICE_WAITING = 0,
    WB_ICE_IN_PROGRESS,
    WB_ICE_SUCCEEDED,
    WB_ICE_FAILED
};

/* The checklist's own state, distinct from a pair's. */
enum {
    WB_ICE_CHECK_RUNNING = 0,
    WB_ICE_CHECK_COMPLETED,
    WB_ICE_CHECK_FAILED
};

typedef struct {
    int           type;
    int           component;
    uint32_t      priority;
    uint32_t      foundation;
    uint32_t      id;
    wb_stun_addr  addr;
    wb_stun_addr  base;          /* the candidate this one was derived from */
} wb_ice_candidate;

typedef struct {
    uint32_t local;              /* candidate id */
    uint32_t remote;
    uint64_t priority;
    int      state;
} wb_ice_pair;

typedef struct wb_ice wb_ice;

/* `controlling` selects which side's priority is G in the pair formula. */
wb_ice *wb_ice_new(int controlling);
void wb_ice_free(wb_ice *ice);

/* Add a candidate. `base` may be NULL (it then defaults to `addr`). Returns the
 * candidate id, or -1 if the type or address is bad. */
int wb_ice_add_local(wb_ice *ice, int type, int component,
                     const wb_stun_addr *addr, const wb_stun_addr *base);
int wb_ice_add_remote(wb_ice *ice, int type, int component,
                      const wb_stun_addr *addr, const wb_stun_addr *base);

/* Eliminate a redundant candidate: same address, same port and same type as one
 * already present. RFC 8445 section 5.1.3. Returns 1 if one was removed. */
int wb_ice_remove_redundant(wb_ice *ice, int type, const wb_stun_addr *addr);

/* Compute priorities, foundations and pairs, and order the checklist. Called
 * automatically by the first check, or explicitly after adding candidates. */
void wb_ice_prioritize(wb_ice *ice);

size_t wb_ice_local(const wb_ice *ice);
size_t wb_ice_remote(const wb_ice *ice);
const wb_ice_candidate *wb_ice_local_at(const wb_ice *ice, size_t i);
const wb_ice_candidate *wb_ice_remote_at(const wb_ice *ice, size_t i);

size_t wb_ice_pairs(const wb_ice *ice);
const wb_ice_pair *wb_ice_pair_at(const wb_ice *ice, size_t i);
int wb_ice_pair_state(const wb_ice *ice, uint32_t local, uint32_t remote);

/* The reachability oracle: would a check on this pair succeed? A real agent
 * answers this with a STUN Binding transaction; the tests answer it with a
 * table, which is exactly why it is a parameter. */
typedef int (*wb_ice_reachable_fn)(void *ud, const wb_ice_candidate *local,
                                   const wb_ice_candidate *remote);

/* Run the next check: the highest-priority pair still Waiting. Returns 1 if a
 * check was run (and its result recorded), 0 if there is nothing to check. */
int wb_ice_check_next(wb_ice *ice, wb_ice_reachable_fn reach, void *ud);

/* Run checks until the checklist settles. Returns the final state. */
int wb_ice_run(wb_ice *ice, wb_ice_reachable_fn reach, void *ud);

int wb_ice_state(const wb_ice *ice);

/* The nominated pair: the highest-priority Succeeded one, or NULL. */
const wb_ice_pair *wb_ice_nominated(const wb_ice *ice);

/* Candidate priority and pair priority, exposed so the formulas can be checked
 * against the RFC directly. */
uint32_t wb_ice_candidate_priority(int type, uint32_t local_pref, int component);
uint64_t wb_ice_pair_priority(uint64_t g, uint64_t d);
uint32_t wb_ice_type_pref(int type);

#ifdef __cplusplus
}
#endif

#endif /* WB_ICE_H */
