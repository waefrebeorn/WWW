/*
 * wb_reverb.c -- Schroeder FDN reverb. Taken from Big Mac
 * (waefrebeorn/big-mac, src/wb_reverb.c); the fixed-stack-buffer `_inplace_wet`
 * helpers are dropped and the two channels are kept apart.
 */
#include "wb_reverb.h"

#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void wb_reverb_init(wb_reverb *r, uint32_t sr) {
    if (!r) return;
    for (int i = 0; i < WB_REVERB_NCOMBS; i++)
        for (int c = 0; c < 2; c++) r->combs[i][c] = NULL;
    for (int i = 0; i < WB_REVERB_NALLPASS; i++)
        for (int c = 0; c < 2; c++) r->allpass[i][c] = NULL;
    r->sr = sr ? sr : 24000;
    /* Prime comb lengths (ms) -- spread so they do not ring together. */
    static const float comb_ms[WB_REVERB_NCOMBS] = { 29.7f, 37.1f, 41.1f, 43.7f };
    static const float ap_ms[WB_REVERB_NALLPASS] = { 5.0f, 1.7f };
    for (int i = 0; i < WB_REVERB_NCOMBS; i++) {
        r->comb_len[i] = (uint32_t)(comb_ms[i] * 0.001f * r->sr);
        if (r->comb_len[i] < 1) r->comb_len[i] = 1;
        for (int c = 0; c < 2; c++) {
            r->combs[i][c] = calloc(r->comb_len[i], sizeof(float));
            r->comb_pos[i][c] = 0;
        }
    }
    for (int i = 0; i < WB_REVERB_NALLPASS; i++) {
        r->ap_len[i] = (uint32_t)(ap_ms[i] * 0.001f * r->sr);
        if (r->ap_len[i] < 1) r->ap_len[i] = 1;
        for (int c = 0; c < 2; c++) {
            r->allpass[i][c] = calloc(r->ap_len[i], sizeof(float));
            r->ap_pos[i][c] = 0;
        }
    }
    r->feedback = 0.7f;
    r->mix = 0.25f;
}

void wb_reverb_free(wb_reverb *r) {
    if (!r) return;
    for (int i = 0; i < WB_REVERB_NCOMBS; i++)
        for (int c = 0; c < 2; c++) free(r->combs[i][c]);
    for (int i = 0; i < WB_REVERB_NALLPASS; i++)
        for (int c = 0; c < 2; c++) free(r->allpass[i][c]);
    r->combs[0][0] = NULL;      /* marks the instance as freed */
}

void wb_reverb_set(wb_reverb *r, int param, float v) {
    if (!r) return;
    switch (param) {
    case 0: r->feedback = v; break;
    case 1: r->mix = v; break;
    default: break;
    }
}

static float comb_tick(float *buf, uint32_t len, uint32_t *pos,
                       float input, float fb) {
    float out = buf[*pos];
    buf[*pos] = input + out * fb;
    *pos = (*pos + 1) % len;
    return out;
}

static float allpass_tick(float *buf, uint32_t len, uint32_t *pos,
                          float input, float g) {
    float out = buf[*pos];
    float delayed = -input + out * g;
    buf[*pos] = input + out * g;
    *pos = (*pos + 1) % len;
    return delayed * g + out - input;
}

/* Run one sample through the comb bank and the allpass chain on ONE channel.
 * This is the whole reverb; both entry points below differ only in whether they
 * average the two channels back into one. */
static float rev_tick(wb_reverb *r, int c, float dry) {
    float sum = 0.0f;
    for (int i = 0; i < WB_REVERB_NCOMBS; i++)
        sum += comb_tick(r->combs[i][c], r->comb_len[i],
                         &r->comb_pos[i][c], dry, r->feedback);
    sum /= WB_REVERB_NCOMBS;
    for (int i = 0; i < WB_REVERB_NALLPASS; i++)
        sum = allpass_tick(r->allpass[i][c], r->ap_len[i],
                           &r->ap_pos[i][c], sum, 0.5f);
    return sum;
}

void wb_reverb_process_stereo(wb_reverb *r, float *l, float *rr, uint32_t n) {
    if (!r || !l || !rr || !r->combs[0][0]) return;  /* freed or never init */
    for (uint32_t i = 0; i < n; i++) {
        float dl = l[i], dr = rr[i];
        float wl = rev_tick(r, 0, dl);
        float wr = rev_tick(r, 1, dr);
        l[i]  = dl * (1.0f - r->mix) + wl * r->mix;
        rr[i] = dr * (1.0f - r->mix) + wr * r->mix;
    }
}

void wb_reverb_process(wb_reverb *r, float *x, uint32_t n) {
    if (!r || !x || !r->combs[0][0]) return;
    for (uint32_t i = 0; i < n; i++) {
        float dry = x[i];
        /* A mono signal drives both channels; average them back to one. */
        float wl = rev_tick(r, 0, dry);
        float wr = rev_tick(r, 1, dry);
        x[i] = dry * (1.0f - r->mix) + ((wl + wr) * 0.5f) * r->mix;
    }
}
