/*
 * wb_det.h -- deterministic substrate (LP-03 stone 1).
 *
 * The source of truth for a reproducible simulation: a fixed, seedable PRNG
 * and an order-dependent checksum. Identical across runs, platforms and
 * compilers because both are pure 64-bit unsigned arithmetic (C guarantees
 * wrapping). A replay, a rollback, or a mesh authority compares the checksum;
 * the PRNG is the only randomness a deterministic sim may use.
 */
#ifndef WB_DET_H
#define WB_DET_H

#include <stddef.h>
#include <stdint.h>

/* ---- deterministic PRNG (splitmix64) ---------------------------------- */
typedef struct { uint64_t s; } wb_det_rng;

void     wb_det_rng_seed(wb_det_rng *r, uint64_t seed);
uint64_t wb_det_rng_next(wb_det_rng *r);            /* the full 64-bit value */
uint64_t wb_det_rng_range(wb_det_rng *r, uint64_t n); /* unbiased-ish [0,n) */
double   wb_det_rng_f01(wb_det_rng *r);             /* [0,1) over 53 bits */

/* ---- deterministic checksum (FNV-1a 64-bit) ---------------------------- */
uint64_t wb_det_checksum(const void *data, size_t n);
/* incremental: wb_det_checksum_stream(h, a, an) then (h, b, bn) == one-shot over a||b */
uint64_t wb_det_checksum_stream(uint64_t h, const void *data, size_t n);

#define WB_DET_FNV_OFFSET 0xcbf29ce484222325ULL
#define WB_DET_FNV_PRIME  0x100000001b3ULL

#endif /* WB_DET_H */
