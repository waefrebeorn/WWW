/*
 * wb_x25519.c -- Curve25519 scalar multiplication. See wb_x25519.h.
 *
 * Field: GF(p), p = 2^255 - 19, held as five 51-bit limbs with weights
 * 2^0, 2^51, 2^102, 2^153, 2^204. A carry out of the top limb is worth
 * 2^255, which is 19 modulo p, so it folds back into limb 0 with that factor.
 *
 * Vectors: RFC 7748 section 5.2 (single-shot and iterated) and section 6.1
 * (Diffie-Hellman).
 */
#include "wb_x25519.h"

#include <string.h>

typedef struct { int64_t v[5]; } fe;

#define FE_MASK51 ((int64_t)((int64_t)1 << 51) - 1)

/* p - 2 = 2^255 - 21, little-endian: the inversion exponent. */
static const uint8_t k_p2[32] = {
    0xEB,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F
};

static void fe_carry(fe *h) {
    for (int pass = 0; pass < 4; pass++) {
        int64_t c = 0;
        for (int i = 0; i < 4; i++) {
            h->v[i] += c;
            c = h->v[i] >> 51;          /* arithmetic shift: floor, so negatives reduce too */
            h->v[i] -= c << 51;
        }
        h->v[4] += c;
        c = h->v[4] >> 51;
        h->v[4] -= c << 51;
        h->v[0] += 19 * c;              /* 2^255 ≡ 19 */
    }
}

static void fe_add(fe *h, const fe *f, const fe *g) {
    for (int i = 0; i < 5; i++) h->v[i] = f->v[i] + g->v[i];
    fe_carry(h);
}

/* Subtract with a 2p bias. The carry is what keeps every limb non-negative:
 * a product accumulator with a negative limb would borrow across the slots
 * that are about to be folded, so no limb may enter a multiply negative. */
static void fe_sub(fe *h, const fe *f, const fe *g) {
    h->v[0] = f->v[0] - g->v[0] - 38;
    h->v[1] = f->v[1] - g->v[1];
    h->v[2] = f->v[2] - g->v[2];
    h->v[3] = f->v[3] - g->v[3];
    h->v[4] = f->v[4] - g->v[4] + ((int64_t)1 << 52);
    fe_carry(h);
}

/* Reduce five wide (128-bit) limb values into a canonical fe. The fold can
 * produce limb-sized values well beyond 64 bits -- 19 * t[8] reaches ~2^110 --
 * so it must happen in wide arithmetic before anything is stored in a limb. */
static void fe_from_wide(fe *h, const __int128 w[5]) {
    __int128 c = 0;
    for (int i = 0; i < 4; i++) {
        __int128 v = w[i] + c;
        c = v >> 51;
        h->v[i] = (int64_t)(v - (c << 51));
    }
    __int128 v = w[4] + c;
    c = v >> 51;
    h->v[4] = (int64_t)(v - (c << 51));
    h->v[0] += 19 * (int64_t)c;         /* 2^255 ≡ 19 */
    fe_carry(h);
}

static void fe_mul(fe *h, const fe *f, const fe *g) {
    /* Normalise first: the accumulator's carry cannot borrow across the slots
     * that are about to be folded, so no limb may enter negative. */
    fe a = *f, b = *g;
    fe_carry(&a);
    fe_carry(&b);
    __int128 t[9];
    for (int k = 0; k < 9; k++) t[k] = 0;
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            t[i + j] += (__int128)a.v[i] * (__int128)b.v[j];
        }
    }
    /* Carry up in radix 2^51; slots 5..8 are multiples of 2^255. */
    for (int k = 0; k < 8; k++) {
        int64_t c = (int64_t)(t[k] >> 51);
        t[k] -= (__int128)c << 51;
        t[k + 1] += c;
    }
    __int128 w[5];
    w[0] = t[0] + (__int128)19 * t[5];
    w[1] = t[1] + (__int128)19 * t[6];
    w[2] = t[2] + (__int128)19 * t[7];
    w[3] = t[3] + (__int128)19 * t[8];
    w[4] = t[4];
    fe_from_wide(h, w);
}

static void fe_sq(fe *h, const fe *f) { fe_mul(h, f, f); }

/* Multiply by a small constant, carrying up and folding 2^255 ≡ 19. */
static void fe_mul_small(fe *h, const fe *f, int64_t s) {
    int64_t carry = 0;
    for (int i = 0; i < 5; i++) {
        __int128 t = (__int128)f->v[i] * s + carry;
        int64_t c = (int64_t)(t >> 51);
        h->v[i] = (int64_t)(t - ((__int128)c << 51));
        carry = c;
    }
    h->v[0] += 19 * carry;
    fe_carry(h);
}

static void fe_frombytes(fe *h, const uint8_t s[32]) {
    uint64_t acc = 0;
    int nbits = 0, limb = 0;
    for (int i = 0; i < 32; i++) {
        acc |= (uint64_t)s[i] << nbits;
        nbits += 8;
        while (nbits >= 51 && limb < 5) {
            h->v[limb++] = (int64_t)(acc & (uint64_t)FE_MASK51);
            acc >>= 51;
            nbits -= 51;
        }
    }
    if (limb < 5) h->v[limb++] = (int64_t)acc;
    while (limb < 5) h->v[limb++] = 0;
}

static void fe_tobytes(uint8_t s[32], const fe *f) {
    fe t = *f;
    fe_carry(&t);

    /* Canonicalise: if t >= p then the result is t - p. Adding 19 and checking
     * for a carry out of bit 255 decides that, and when it carries the wrapped
     * limbs are already exactly t - p (because t + 19 - 2^255 = t - p). */
    fe u = t;
    int64_t carry = 19;
    for (int i = 0; i < 5; i++) {
        int64_t v = u.v[i] + carry;
        carry = v >> 51;
        u.v[i] = v - (carry << 51);
    }
    if (carry) t = u;

    for (int i = 0; i < 32; i++) s[i] = 0;
    for (int i = 0; i < 5; i++) {
        for (int b = 0; b < 51; b++) {
            int bit = i * 51 + b;
            if (bit >= 255) break;
            if ((t.v[i] >> b) & 1) s[bit / 8] |= (uint8_t)(1u << (bit % 8));
        }
    }
}

static void fe_cswap(fe *f, fe *g, uint64_t swap) {
    int64_t mask = (int64_t)(0 - swap);
    for (int i = 0; i < 5; i++) {
        int64_t d = mask & (f->v[i] ^ g->v[i]);
        f->v[i] ^= d;
        g->v[i] ^= d;
    }
}

/* out = base^e, e little-endian. Used for the inversion exponent p-2. */
static void fe_pow(fe *out, const fe *base, const uint8_t *e, int nbits) {
    fe r;
    memset(&r, 0, sizeof r);
    r.v[0] = 1;
    for (int i = nbits - 1; i >= 0; i--) {
        fe_sq(&r, &r);
        if ((e[i / 8] >> (i % 8)) & 1) fe_mul(&r, &r, base);
    }
    *out = r;
}

void wb_x25519(uint8_t out[32], const uint8_t scalar[32],
               const uint8_t u[32]) {
    uint8_t k[32];
    memcpy(k, scalar, 32);
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;

    fe x1, x2, z2, x3, z3, A, AA, B, BB, E, C, D, DA, CB, t, t2, inv;
    fe_frombytes(&x1, u);
    memset(&x2, 0, sizeof x2); x2.v[0] = 1;
    memset(&z2, 0, sizeof z2);
    x3 = x1;
    memset(&z3, 0, sizeof z3); z3.v[0] = 1;

    uint64_t swap = 0;
    for (int bit = 254; bit >= 0; bit--) {
        uint64_t kb = (uint64_t)((k[bit / 8] >> (bit % 8)) & 1);
        swap ^= kb;
        fe_cswap(&x2, &x3, swap);
        fe_cswap(&z2, &z3, swap);
        swap = kb;

        fe_add(&A, &x2, &z2);
        fe_sq(&AA, &A);
        fe_sub(&B, &x2, &z2);
        fe_sq(&BB, &B);
        fe_sub(&E, &AA, &BB);
        fe_add(&C, &x3, &z3);
        fe_sub(&D, &x3, &z3);
        fe_mul(&DA, &D, &A);
        fe_mul(&CB, &C, &B);
        fe_add(&t, &DA, &CB);
        fe_sq(&x3, &t);
        fe_sub(&t, &DA, &CB);
        fe_sq(&t, &t);
        fe_mul(&z3, &x1, &t);
        fe_mul(&x2, &AA, &BB);
        fe_mul_small(&t2, &E, 121665);
        fe_add(&t2, &AA, &t2);
        fe_mul(&z2, &E, &t2);
    }
    fe_cswap(&x2, &x3, swap);
    fe_cswap(&z2, &z3, swap);

    fe_pow(&inv, &z2, k_p2, 255);
    fe_mul(&x2, &x2, &inv);
    fe_tobytes(out, &x2);
}

void wb_x25519_public(uint8_t pub[32], const uint8_t priv[32]) {
    uint8_t base[32];
    memset(base, 0, sizeof base);
    base[0] = 9;
    wb_x25519(pub, priv, base);
}

int wb_x25519_shared(uint8_t shared[32], const uint8_t priv[32],
                     const uint8_t peer[32]) {
    wb_x25519(shared, priv, peer);
    uint8_t acc = 0;
    for (int i = 0; i < 32; i++) acc |= shared[i];
    return acc == 0 ? -1 : 0;
}
