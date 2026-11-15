/*
 * wb_ice.c -- LP-95 stone 2: ICE, the checklist.
 */
#include "wb_ice.h"

#include <stdlib.h>
#include <string.h>

#define WB_ICE_MAX_CANDIDATES 64
#define WB_ICE_MAX_PAIRS     (WB_ICE_MAX_CANDIDATES * WB_ICE_MAX_CANDIDATES)

struct wb_ice {
    int controlling;

    wb_ice_candidate local[WB_ICE_MAX_CANDIDATES];
    int  n_local;
    wb_ice_candidate remote[WB_ICE_MAX_CANDIDATES];
    int  n_remote;

    wb_ice_pair pairs[WB_ICE_MAX_PAIRS];
    int  n_pairs;

    int  state;
    int  prioritised;
};

uint32_t wb_ice_type_pref(int type) {
    switch (type) {
    case WB_ICE_HOST:            return WB_ICE_TYPE_PREF_HOST;
    case WB_ICE_PEER_REFLEXIVE:  return WB_ICE_TYPE_PREF_PEER_REFLEXIVE;
    case WB_ICE_SERVER_REFLEXIVE:return WB_ICE_TYPE_PREF_SERVER_REFLEXIVE;
    case WB_ICE_RELAYED:         return WB_ICE_TYPE_PREF_RELAYED;
    default:                     return 0;
    }
}

uint32_t wb_ice_candidate_priority(int type, uint32_t local_pref, int component) {
    if (component < 1) component = 1;
    if (component > 256) component = 256;
    uint32_t tp = wb_ice_type_pref(type);
    return (tp << 24) + (local_pref << 8) + (uint32_t)(256 - component);
}

uint64_t wb_ice_pair_priority(uint64_t g, uint64_t d) {
    uint64_t lo = g < d ? g : d;
    uint64_t hi = g < d ? d : g;
    return (lo << 32) + 2u * hi + (g > d ? 1u : 0u);
}

wb_ice *wb_ice_new(int controlling) {
    wb_ice *ice = (wb_ice *)calloc(1, sizeof *ice);
    if (!ice) return NULL;
    ice->controlling = controlling ? 1 : 0;
    ice->state = WB_ICE_CHECK_RUNNING;
    return ice;
}

void wb_ice_free(wb_ice *ice) { free(ice); }

static int addr_family_ok(const wb_stun_addr *a) {
    return a && (a->family == 4 || a->family == 6);
}

/* A foundation is a property of the base address and the candidate type: two
 * candidates off the same base stand or fall together, so they share one. */
static uint32_t foundation_of(int type, const wb_stun_addr *base) {
    /* RFC 8445 section 5.1.3: two candidates off the same IP address share a
     * foundation, so the port is deliberately not part of it. */
    uint32_t h = 0x9E3779B9u ^ (uint32_t)type;
    size_t n = (base->family == 6) ? 16u : 4u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ base->addr[i]) * 0x01000193u;
    }
    return h;
}

static int add_cand(wb_ice *ice, int remote, int type, int component,
                    const wb_stun_addr *addr, const wb_stun_addr *base) {
    if (type < 0 || type > WB_ICE_RELAYED) return -1;
    if (!addr_family_ok(addr)) return -1;
    if (component < 1 || component > 256) return -1;

    wb_ice_candidate *arr = remote ? ice->remote : ice->local;
    int *n = remote ? &ice->n_remote : &ice->n_local;
    if (*n >= WB_ICE_MAX_CANDIDATES) return -1;

    wb_ice_candidate *c = &arr[*n];
    memset(c, 0, sizeof *c);
    c->type = type;
    c->component = component;
    c->addr = *addr;
    c->base = base ? *base : *addr;
    c->id = (uint32_t)(*n);
    c->priority = wb_ice_candidate_priority(type, 65535, component);
    c->foundation = foundation_of(type, &c->base);
    (*n)++;
    ice->prioritised = 0;
    return (int)c->id;
}

int wb_ice_add_local(wb_ice *ice, int type, int component,
                     const wb_stun_addr *addr, const wb_stun_addr *base) {
    if (!ice) return -1;
    return add_cand(ice, 0, type, component, addr, base);
}

int wb_ice_add_remote(wb_ice *ice, int type, int component,
                      const wb_stun_addr *addr, const wb_stun_addr *base) {
    if (!ice) return -1;
    return add_cand(ice, 1, type, component, addr, base);
}

int wb_ice_remove_redundant(wb_ice *ice, int type, const wb_stun_addr *addr) {
    if (!ice || !addr) return 0;
    int removed = 0;
    for (int side = 0; side < 2; side++) {
        wb_ice_candidate *arr = side ? ice->remote : ice->local;
        int *n = side ? &ice->n_remote : &ice->n_local;
        for (int i = 0; i < *n; i++) {
            if (arr[i].type != type || arr[i].addr.family != addr->family ||
                arr[i].addr.port != addr->port) {
                continue;
            }
            size_t alen = (addr->family == 6) ? 16u : 4u;
            if (memcmp(arr[i].addr.addr, addr->addr, alen) != 0) continue;
            for (int j = i; j + 1 < *n; j++) arr[j] = arr[j + 1];
            (*n)--;
            removed = 1;
            /* ids are positions, so renumber the tail */
            for (int j = i; j < *n; j++) arr[j].id = (uint32_t)j;
            break;
        }
    }
    if (removed) ice->prioritised = 0;
    return removed;
}

size_t wb_ice_local(const wb_ice *ice) { return (size_t)ice->n_local; }
size_t wb_ice_remote(const wb_ice *ice) { return (size_t)ice->n_remote; }
const wb_ice_candidate *wb_ice_local_at(const wb_ice *ice, size_t i) {
    return (i < (size_t)ice->n_local) ? &ice->local[i] : NULL;
}
const wb_ice_candidate *wb_ice_remote_at(const wb_ice *ice, size_t i) {
    return (i < (size_t)ice->n_remote) ? &ice->remote[i] : NULL;
}
size_t wb_ice_pairs(const wb_ice *ice) { return (size_t)ice->n_pairs; }
const wb_ice_pair *wb_ice_pair_at(const wb_ice *ice, size_t i) {
    return (i < (size_t)ice->n_pairs) ? &ice->pairs[i] : NULL;
}

static const wb_ice_candidate *find_local(wb_ice *ice, uint32_t id) {
    for (int i = 0; i < ice->n_local; i++) if (ice->local[i].id == id) return &ice->local[i];
    return NULL;
}

static const wb_ice_candidate *find_remote(wb_ice *ice, uint32_t id) {
    for (int i = 0; i < ice->n_remote; i++) if (ice->remote[i].id == id) return &ice->remote[i];
    return NULL;
}

static int pair_cmp(const void *a, const void *b) {
    const wb_ice_pair *pa = (const wb_ice_pair *)a;
    const wb_ice_pair *pb = (const wb_ice_pair *)b;
    if (pa->priority > pb->priority) return -1;
    if (pa->priority < pb->priority) return 1;
    return 0;
}

void wb_ice_prioritize(wb_ice *ice) {
    ice->n_pairs = 0;
    for (int i = 0; i < ice->n_local; i++) {
        for (int j = 0; j < ice->n_remote; j++) {
            const wb_ice_candidate *l = &ice->local[i];
            const wb_ice_candidate *r = &ice->remote[j];
            /* pairs need the same component and the same address family */
            if (l->component != r->component) continue;
            if (l->addr.family != r->addr.family) continue;
            if (ice->n_pairs >= WB_ICE_MAX_PAIRS) break;
            wb_ice_pair *p = &ice->pairs[ice->n_pairs];
            p->local = l->id;
            p->remote = r->id;
            /* G is the controlling agent's candidate, D the controlled one */
            uint64_t g = ice->controlling ? l->priority : r->priority;
            uint64_t d = ice->controlling ? r->priority : l->priority;
            p->priority = wb_ice_pair_priority(g, d);
            p->state = WB_ICE_WAITING;
            ice->n_pairs++;
        }
    }
    qsort(ice->pairs, (size_t)ice->n_pairs, sizeof ice->pairs[0], pair_cmp);
    ice->prioritised = 1;
}

int wb_ice_pair_state(const wb_ice *ice, uint32_t local, uint32_t remote) {
    for (int i = 0; i < ice->n_pairs; i++) {
        if (ice->pairs[i].local == local && ice->pairs[i].remote == remote) {
            return ice->pairs[i].state;
        }
    }
    return -1;
}

int wb_ice_check_next(wb_ice *ice, wb_ice_reachable_fn reach, void *ud) {
    if (!ice) return 0;
    if (!ice->prioritised) wb_ice_prioritize(ice);

    for (int i = 0; i < ice->n_pairs; i++) {
        wb_ice_pair *p = &ice->pairs[i];
        if (p->state != WB_ICE_WAITING) continue;
        const wb_ice_candidate *l = find_local(ice, p->local);
        const wb_ice_candidate *r = find_remote(ice, p->remote);
        if (!l || !r) { p->state = WB_ICE_FAILED; continue; }
        p->state = WB_ICE_IN_PROGRESS;
        p->state = reach && reach(ud, l, r) ? WB_ICE_SUCCEEDED : WB_ICE_FAILED;
        return 1;
    }
    return 0;
}

int wb_ice_state(const wb_ice *ice) {
    if (!ice) return WB_ICE_CHECK_FAILED;
    if (!ice->prioritised) return WB_ICE_CHECK_RUNNING;
    int any_succeeded = 0, any_failed = 0;
    for (int i = 0; i < ice->n_pairs; i++) {
        if (ice->pairs[i].state == WB_ICE_SUCCEEDED) any_succeeded = 1;
        if (ice->pairs[i].state == WB_ICE_FAILED) any_failed = 1;
    }
    if (any_succeeded) return WB_ICE_CHECK_COMPLETED;
    if (ice->n_pairs > 0 && any_failed) return WB_ICE_CHECK_FAILED;
    return WB_ICE_CHECK_RUNNING;
}

int wb_ice_run(wb_ice *ice, wb_ice_reachable_fn reach, void *ud) {
    if (!ice) return WB_ICE_CHECK_FAILED;
    if (!ice->prioritised) wb_ice_prioritize(ice);
    /* Regular nomination: the whole checklist is checked, then the best valid
     * pair is nominated. Aggressive nomination would stop at the first success;
     * the difference is only how long the peer waits, and checking everything
     * is what makes the MAX/MIN ordering observable. */
    int guard = 0;
    while (wb_ice_check_next(ice, reach, ud) && guard++ < WB_ICE_MAX_PAIRS * 2) {
    }
    return wb_ice_state(ice);
}

const wb_ice_pair *wb_ice_nominated(const wb_ice *ice) {
    if (!ice) return NULL;
    /* the checklist is already sorted by descending priority, so the first
     * Succeeded pair is the one to nominate */
    for (int i = 0; i < ice->n_pairs; i++) {
        if (ice->pairs[i].state == WB_ICE_SUCCEEDED) return &ice->pairs[i];
    }
    return NULL;
}
