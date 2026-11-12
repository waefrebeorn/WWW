/*
 * wb_env.h -- ADSR envelope generator.
 *
 * Taken from the Big Mac repo (waefrebeorn/big-mac, src/wb_env.c), the WuBu
 * musical agent's DAW. Same author, same no-third-party rule.
 *
 * The engine's audio used to shape a sound with a hand-rolled (1-u)^2 decay.
 * Big Mac's envelope has four stages and a SUSTAIN level, which is the difference
 * between a blip and a note you can hold: a footfall is an attack-decay, a score
 * is an attack-sustain-release.
 *
 * One adaptation: Big Mac's decay and release are exponential with a time
 * constant equal to the whole stage, so a note is still 37% away from its target
 * when the stage ends -- right for a held note in a DAW, wrong for a one-shot
 * that must be over by its stated duration. The engine runs WB_ENV_SETTLE time
 * constants in the stage instead, so the decay has settled by `d` and the release
 * has crossed the silence floor by ~1.7*r.
 *
 * C11, no deps.
 */
#ifndef WB_ENV_H
#define WB_ENV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Time constants spent per stage, in samples. 3 is 95% of the way: enough that
 * the decay->sustain snap is not a click, not so fast that a footfall is a
 * one-period blip no pitch detector can read. */
#define WB_ENV_SETTLE 3.0f

typedef struct wb_env {
    float  a, d, s, r;   /* attack, decay, sustain level, release -- seconds */
    float  sr;           /* sample rate */
    double level;        /* current output, 0..1 */
    int    stage;        /* 0 idle, 1 attack, 2 decay, 3 sustain, 4 release */
    int    note_on;
    double cur;          /* stage clock, in samples */
} wb_env;

void wb_env_init(wb_env *e, float sr);
void wb_env_note_on(wb_env *e, float a, float d, float s, float r);
void wb_env_note_off(wb_env *e);
float wb_env_process(wb_env *e);

#ifdef __cplusplus
}
#endif

#endif /* WB_ENV_H */
