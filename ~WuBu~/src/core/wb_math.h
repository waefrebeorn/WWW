/*
 * wb_math.h -- deterministic sim math (LP-03 final stone).
 *
 * Q31.32 fixed-point and integer-only CORDIC trigonometry: pure 64-bit integer
 * arithmetic, so identical on any C11 host regardless of the FPU or float
 * mode. That is the last link in LP-03 -- the simulation core no longer
 * depends on host float behavior, so a replay reproduces bit-for-bit.
 *
 * Angles are measured in TURNS, where one turn = 2^32. That makes angle
 * math exact and periodicity free (add a full turn and nothing changes).
 * Trig results are unit Q31.32 in [-1, 1].
 */
#ifndef WB_MATH_H
#define WB_MATH_H

#include <stdint.h>

typedef int64_t wb_fix;

#define WB_FIX_Q     32
#define WB_FIX_ONE   ((int64_t)1 << 32)   /* 1.0, and one turn of angle */
#define WB_FIX_HALF  ((int64_t)1 << 31)   /* 0.5 */
#define WB_FIX_QUART ((int64_t)1 << 30)   /* 0.25 turn / 90 degrees */

/* Q31.32 arithmetic (floor, deterministic) */
int64_t wb_fix_mul(int64_t a, int64_t b);
int64_t wb_fix_div(int64_t a, int64_t b);
int64_t wb_fix_from_double(double x);
double  wb_fix_to_double(int64_t x);

/* integer-only CORDIC; angle in turns (2^32 = one turn). */
int64_t wb_fix_sin(int64_t angle);
int64_t wb_fix_cos(int64_t angle);

#endif /* WB_MATH_H */
