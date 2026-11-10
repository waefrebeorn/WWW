/*
 * wb_det.c -- deterministic substrate (LP-03 stone 1). See wb_det.h.
 * Pure unsigned arithmetic: identical outputs on any C11 host.
 */
#include "wb_det.h"

void wb_det_rng_seed(wb_det_rng *r, uint64_t seed) {
    if (r) r->s = seed;
}

uint64_t wb_det_rng_next(wb_det_rng *r) {
    if (!r) return 0;
    uint64_t z = (r->s += 0x9e3779b97f4a7c15ULL);      /* state advance, wraps */
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

uint64_t wb_det_rng_range(wb_det_rng *r, uint64_t n) {
    if (!r || n == 0) return 0;
    /* multiply-high (Lemire): unbiased over [0,n) without rejection loops */
    return (uint64_t)(((unsigned __int128)wb_det_rng_next(r) * (unsigned __int128)n) >> 64);
}

double wb_det_rng_f01(wb_det_rng *r) {
    /* top 53 bits -> [0,1), the usual fp-friendly split */
    return (double)(wb_det_rng_next(r) >> 11) * (1.0 / 9007199254740992.0);
}

uint64_t wb_det_checksum(const void *data, size_t n) {
    return wb_det_checksum_stream(WB_DET_FNV_OFFSET, data, n);
}

uint64_t wb_det_checksum_stream(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint64_t)p[i];
        h *= WB_DET_FNV_PRIME;
    }
    return h;
}

void wb_det_fixed_init(wb_det_fixed *f, double step) {
    if (f) { f->step = (step > 0.0) ? step : (1.0 / 64.0); f->acc = 0.0; }
}

int wb_det_fixed_advance(wb_det_fixed *f, double dt) {
    if (!f || f->step <= 0.0) return 0;
    if (dt < 0.0) dt = 0.0;
    f->acc += dt;
    long long steps = (long long)(f->acc / f->step);
    if (steps < 0) steps = 0;
    f->acc -= (double)steps * f->step;
    if (f->acc < 0.0) f->acc = 0.0;                    /* rounding can go tiny negative */
    if (f->acc >= f->step) { f->acc = 0.0; steps++; }  /* rounding can overshoot the step */
    return (int)steps;
}

double wb_det_fixed_remainder(const wb_det_fixed *f) { return f ? f->acc : 0.0; }
