/*
 * wb_math.c -- deterministic sim math (LP-03 final stone). See wb_math.h.
 * All integer. The CORDIC gain/atan tables were generated once with exact
 * high-precision constants; any re-derivation must reproduce them.
 */
#include "wb_math.h"

#include <math.h>

#define WB_FIX_TAU 26986075409LL   /* 2*pi in Q32                    */
#define WB_FIX_K   2608131496LL    /* 1/CORDIC-gain (0.607252935..)  */

#define WB_FIX_ATAN_LEN 34
static const int64_t WB_FIX_ATAN[WB_FIX_ATAN_LEN] = {
    0x00000000c90fdaa2LL, 0x0000000076b19c16LL, 0x000000003eb6ebf2LL,
    0x000000001fd5ba9bLL, 0x000000000ffaaddcLL, 0x0000000007ff556fLL,
    0x0000000003ffeaabLL, 0x0000000001fffd55LL, 0x0000000000ffffabLL,
    0x00000000007ffff5LL, 0x00000000003fffffLL, 0x0000000000200000LL,
    0x0000000000100000LL, 0x0000000000080000LL, 0x0000000000040000LL,
    0x0000000000020000LL, 0x0000000000010000LL, 0x0000000000008000LL,
    0x0000000000004000LL, 0x0000000000002000LL, 0x0000000000001000LL,
    0x0000000000000800LL, 0x0000000000000400LL, 0x0000000000000200LL,
    0x0000000000000100LL, 0x0000000000000080LL, 0x0000000000000040LL,
    0x0000000000000020LL, 0x0000000000000010LL, 0x0000000000000008LL,
    0x0000000000000004LL, 0x0000000000000002LL, 0x0000000000000001LL,
    0x0000000000000000LL,
};

int64_t wb_fix_mul(int64_t a, int64_t b) {
    return (int64_t)(((__int128)a * b) >> WB_FIX_Q);
}

int64_t wb_fix_div(int64_t a, int64_t b) {
    if (b == 0) return 0;
    return (int64_t)(((__int128)a << WB_FIX_Q) / b);
}

int64_t wb_fix_from_double(double x) { return (int64_t)llround(x * 4294967296.0); }
double  wb_fix_to_double(int64_t x)  { return (double)x * 0x1p-32; }

/* CORDIC rotation to bring the angle to zero, in the first quadrant.
 * Each rotation must use the OLD x for y's update (rotational symmetry):
 *   xt = x - s*(y>>i); yt = y + s*(x>>i); then x=xt,y=yt.            */
static void wb_fix_sincos_quad(int64_t qangle, int64_t *pc, int64_t *ps) {
    int64_t x = WB_FIX_K, y = 0, z = qangle;
    for (int i = 0; i < WB_FIX_ATAN_LEN; i++) {
        int64_t ai = WB_FIX_ATAN[i];
        if (z >= 0) { int64_t xt = x - (y >> i); y += x >> i; x = xt; z -= ai; }
        else        { int64_t xt = x + (y >> i); y -= x >> i; x = xt; z += ai; }
    }
    *pc = x;
    *ps = y;
}

int64_t wb_fix_sin(int64_t angle) {
    uint64_t ua = (uint64_t)angle;
    unsigned quad = (unsigned)((ua >> 30) & 3u);
    uint64_t a32 = ua & 0x3fffffffu;                  /* [0, 1/4] turn */
    /* Q32 radians; __int128 so angles up to a quarter turn never overflow */
    int64_t z = (int64_t)(((__int128)a32 * (int64_t)WB_FIX_TAU) >> 32);
    int64_t c, s;
    wb_fix_sincos_quad(z, &c, &s);
    switch (quad) {
        case 0:  return s;                       /* cos+, sin+ */
        case 1:  return c;                       /* cos-, sin+ (sin<->cos swap) */
        case 2:  return -s;                      /* cos-, sin- */
        default: return -c;                      /* cos+, sin- */
    }
}

int64_t wb_fix_cos(int64_t angle) { return wb_fix_sin(angle + WB_FIX_QUART); }
