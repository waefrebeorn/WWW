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

/* ---- fixed timestep accumulator (LP-03/LP-04 time base) -----------------
 * Advances a simulation step clock from a stream of real elapsed times without
 * ever reading the wall clock. Whole steps are consumed and returned; the
 * carried remainder is the render-interpolation target. Pure IEEE double
 * arithmetic, so identical on any host. A power-of-two-ish rate (64 Hz =
 * 1/64 s) makes whole steps land exactly; 60 Hz does not. */
typedef struct { double step; double acc; } wb_det_fixed;

void   wb_det_fixed_init(wb_det_fixed *f, double frame_seconds);
int    wb_det_fixed_advance(wb_det_fixed *f, double dt); /* #whole steps, dt>=0 */
double wb_det_fixed_remainder(const wb_det_fixed *f);    /* [0,step) carried time */

#endif /* WB_DET_H */
