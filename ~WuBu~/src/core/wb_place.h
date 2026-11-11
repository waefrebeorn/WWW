/*
 * wb_place.h -- instance placement at a level (LP-105 stone 1).
 *
 * Roblox hosts a server per place, WoW hosts instances on a realm, EVE dedicates
 * a node to one solar system. Those are three answers to the same question: given
 * a request for an instance of a gametype at a place, WHICH node runs it, and is
 * that node exclusive to the instance?
 *
 * The policy is two orthogonal flags:
 *
 *   WB_PLACE_DEDICATED on a NODE   -> the node holds at most one instance,
 *                                      whatever its capacity says.
 *   WB_PLACE_DEDICATED on a REQUEST -> the instance needs a node that is
 *                                      completely empty.
 *
 * A node serves either one address or WB_PLACE_ANY (a general-purpose node). A
 * request naming an address is placed on a node serving that address, or on a
 * general-purpose one. Ties break to the LOWEST node id, so two servers running
 * the same pool make the same placement without consulting each other.
 *
 * C11, no deps. Deterministic.
 */
#ifndef WB_PLACE_H
#define WB_PLACE_H

#include <stdint.h>

#define WB_PLACE_ANY 0
#define WB_PLACE_MAX_PER_NODE 16

#define WB_PLACE_DEDICATED (1u << 0)

typedef struct {
    uint64_t id;
    uint64_t address;                        /* the place this node serves, or WB_PLACE_ANY */
    uint32_t capacity;                       /* max instances, if not dedicated */
    uint32_t flags;                          /* WB_PLACE_* */
    uint64_t instances[WB_PLACE_MAX_PER_NODE];
    uint32_t n;
} wb_place_node;

typedef struct {
    wb_place_node *nodes;
    uint32_t       cap;
    uint32_t       n;
} wb_place_pool;

typedef struct {
    uint64_t id;                             /* the instance id to place */
    uint64_t address;                        /* the requested place, or WB_PLACE_ANY */
    uint32_t flags;                          /* WB_PLACE_DEDICATED to require an empty node */
} wb_place_request;

typedef enum {
    WB_PLACE_OK = 0,
    WB_PLACE_REFUSED_NO_NODE,       /* nothing serves that address */
    WB_PLACE_REFUSED_FULL,          /* candidates exist but every one is full */
    WB_PLACE_REFUSED_EXCLUSIVE,     /* candidates exist but none is empty */
    WB_PLACE_REFUSED_DUPLICATE      /* the instance id is already placed */
} wb_place_reason;

typedef struct {
    uint64_t node;                  /* 0 when refused */
    uint32_t reason;                /* a wb_place_reason */
} wb_place_result;

int  wb_place_init(wb_place_pool *p, uint32_t cap);
void wb_place_free(wb_place_pool *p);
void wb_place_clear(wb_place_pool *p);

/* Add a node. Returns 0 on success, -1 on a duplicate id, a full pool, or a
 * capacity above WB_PLACE_MAX_PER_NODE. */
int  wb_place_add(wb_place_pool *p, uint64_t id, uint64_t address, uint32_t capacity, uint32_t flags);

wb_place_result wb_place_alloc(wb_place_pool *p, const wb_place_request *r);

/* Release an instance from a node. Returns 0 on success, -1 if the node or the
 * instance is unknown. */
int  wb_place_release(wb_place_pool *p, uint64_t node, uint64_t instance);

uint32_t wb_place_load(const wb_place_pool *p, uint64_t node);
uint32_t wb_place_slots(const wb_place_pool *p, uint64_t node);   /* the effective capacity */
int      wb_place_contains(const wb_place_pool *p, uint64_t node, uint64_t instance);

#endif /* WB_PLACE_H */
