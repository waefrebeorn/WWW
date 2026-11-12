/*
 * wb_atmos.h -- HRTF binaural 3D audio panner: the atmosphere a sound sits in.
 *
 * Taken from the Big Mac repo (waefrebeorn/big-mac, src/wb_spatial_audio.c), the
 * WuBu musical agent's DAW. Same author, same no-third-party rule.
 *
 * The name is deliberate: the engine already has wb_spatial, which is the
 * quadkey addressing scheme (LP-02). This module is not about WHERE things are
 * addressed -- it is about where they are HEARD.
 *
 * Simplified parametric HRTF (no database):
 *   - ITD: interaural time delay from azimuth and head radius (~8.75 cm)
 *   - IID: frequency-averaged head shadowing on the far ear
 *   - Elevation: a pinna notch around 4-8 kHz that shifts with elevation
 *   - Distance: inverse-square attenuation plus a one-pole air-absorption lowpass
 *   - Binaural: per-ear delay and gain; or VBAP constant-power pan when off
 *   - Room: a six-tap early-reflection model fed from the room size
 *
 * This is what turns the engine's audio from a beep into a PLACE. A footfall
 * behind you is not the same sound as a footfall in front of you, and without
 * this the mix is a flat sheet at the listener's head.
 *
 * C11, no deps beyond libm.
 */
#ifndef WB_ATMOS_H
#define WB_ATMOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_atmos wb_atmos;

wb_atmos *wb_atmos_create(uint32_t sr);
void  wb_atmos_free(wb_atmos *sp);

/* Source position: azimuth in degrees (-180..180, 0 = front), elevation in
 * degrees (-90..90, 0 = horizon), distance in metres (>= 0.1). */
void  wb_atmos_set_position(wb_atmos *sp, float azimuth,
                            float elevation, float distance);

/* Listener orientation in degrees. Yaw turns the head, which shifts every
 * source's effective azimuth. */
void  wb_atmos_set_listener(wb_atmos *sp, float yaw, float pitch, float roll);

/* 1 = HRTF binaural (per-ear delay and gain), 0 = VBAP constant-power pan. */
void  wb_atmos_set_binaural(wb_atmos *sp, int enable);

/* The room: reverb level 0..1 and wall distance in metres. */
void  wb_atmos_set_room(wb_atmos *sp, float reverb_level, float room_size);

/* Pan `frames` mono samples into a stereo pair. `in` must have `frames` samples;
 * `out_l` and `out_r` must each have room for `frames` and must not alias `in`. */
void  wb_atmos_process(wb_atmos *sp, const float *in,
                       float *out_l, float *out_r, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* WB_ATMOS_H */
