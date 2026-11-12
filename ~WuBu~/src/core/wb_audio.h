/*
 * wb_audio.h -- WuBu Engine audio: a synthesiser and a mixer.
 *
 * The floor, not the studio. There is no asset format, no device and no
 * streaming: a sound is a small set of parameters, a voice is a phase and a
 * clock, and mixing is a sum. That is enough to make a game AUDIBLE -- a footfall
 * per stride, a tone when the score changes -- without a content pipeline, which
 * is the same reason wb_walk exists before the animation clips do.
 *
 * The synthesis itself is Big Mac's. The oscillator (poly-BLEP saw and square,
 * sine, triangle, noise), the ADSR envelope, the RBJ biquad and the Schroeder FDN
 * reverb are taken from waefrebeorn/big-mac, the WuBu musical agent's DAW. Same
 * author, same no-third-party rule. This module is the SEAM: it keeps Big Mac's
 * units and gives them the shape a game needs -- one-shot events rather than held
 * notes, and a STEREO, POSITIONAL mix rather than a flat mono sheet.
 *
 * What it deliberately is not: it does not play out of a device, does not stream,
 * does not do 3D attenuation or Doppler, and has no sample formats. A caller that
 * wants to hear it writes the mix to a file (the engine writes a WAV); a caller
 * with a device hands the same float buffer to it.
 *
 * C11, no external deps beyond libm.
 */
#ifndef WB_AUDIO_H
#define WB_AUDIO_H

#include <stdint.h>

#include "wb_osc.h"
#include "wb_env.h"
#include "wb_biquad.h"
#include "wb_reverb.h"
#include "wb_atmos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One voice per simultaneous sound. Eight is plenty for a floor: a footfall is
 * 80 ms, so eight of them overlap only if a game is spraying events faster than
 * a human can hear them. */
#define WB_AUDIO_MAX_VOICES 8

typedef enum {
    WB_SND_FOOTSTEP = 0,  /* a short low thud, one per footfall */
    WB_SND_JUMP,          /* a rising blip */
    WB_SND_LAND,          /* a low thud, longer than the footstep */
    WB_SND_TAG,           /* a bright ping */
    WB_SND_SCORE,         /* a longer rising tone */
    WB_SND_COUNT
} wb_snd;

typedef struct {
    int      active;
    wb_snd   sound;
    double   t;           /* seconds into the voice */
    double   inc;         /* current phase increment, radians per sample */
    double   hold;        /* seconds of sustain before the release is triggered */
    float    azimuth;     /* where it is heard: degrees, 0 = in front */
    float    elevation;   /* degrees, 0 = on the horizon */
    float    distance;    /* metres */
    wb_osc   osc;
    wb_env   env;
    wb_atmos *atmos;      /* the panner for this voice */
} wb_voice;

typedef struct {
    int       rate;                        /* samples per second */
    wb_voice  voices[WB_AUDIO_MAX_VOICES];
    wb_biquad filter_l;                    /* master lowpass, left */
    wb_biquad filter_r;                    /* master lowpass, right */
    wb_reverb reverb;                      /* master FDN send, stereo */
    float     yaw, pitch, roll;            /* where the listener is looking */
    float     *scratch;                    /* voice render scratch, nframes */
    int       scratch_n;
    uint64_t  emitted;                     /* events ever accepted */
    uint64_t  dropped;                     /* events refused: mixer was full */
} wb_audio;

/* `rate` must be positive. 24000 divides evenly by the 60 Hz fixed step (400
 * samples per tick), which keeps the audio clock locked to the simulation clock
 * instead of drifting against it. */
void     wb_audio_init(wb_audio *a, int rate);

/* Release the reverb's buffers. A wb_audio embedded in a larger struct must be
 * freed with this rather than left to leak. */
void     wb_audio_free(wb_audio *a);

/* Start a sound. Returns 0 on success, -1 when every voice is busy -- the event
 * is DROPPED rather than stealing a voice mid-note, so a loud moment cannot
 * silently truncate a sound that is still playing. A caller that cares counts
 * `dropped`. */
int      wb_audio_emit(wb_audio *a, wb_snd s);

/* Start a sound AT A PLACE. `azimuth` is degrees, 0 = straight ahead, negative
 * = left; `elevation` is degrees above the horizon; `distance` is metres. This is
 * the whole difference between a beep and a world: the same footfall behind you
 * is not the same sound as the one in front of you. */
int      wb_audio_emit_at(wb_audio *a, wb_snd s, float azimuth,
                          float elevation, float distance);

uint64_t wb_audio_emitted(const wb_audio *a);
uint64_t wb_audio_dropped(const wb_audio *a);

/* Voices still sounding right now. */
int      wb_audio_active(const wb_audio *a);

/* Render `nframes` of STEREO samples in [-1,1] into `out_l` and `out_r`. Every
 * voice is placed by its own HRTF panner, so a sound's position survives to the
 * ear; the mix is the two channels summed, filtered, and sent to a stereo FDN.
 * Silent input produces exact zeros on both channels. */
void     wb_audio_mix(wb_audio *a, float *out_l, float *out_r, int nframes);

/* Where the listener is looking, in degrees. Yaw turns the head and shifts every
 * source's effective azimuth, which is what makes looking around work. */
void     wb_audio_set_listener(wb_audio *a, float yaw, float pitch, float roll);

/* The room every voice sits in: a reverb level 0..1 and a wall distance in
 * metres. Set once, or change it when the player walks into a hall. */
void     wb_audio_set_room(wb_audio *a, float reverb_level, float room_size);

/* The master chain. type: 0 lowpass, 1 highpass, 2 bandpass, 3 notch. */
void     wb_audio_set_filter(wb_audio *a, int type, float freq, float q);
void     wb_audio_set_reverb(wb_audio *a, float feedback, float mix);

/* The parameters of a sound: a glide from freq0 to freq1 over dur seconds at amp
 * peak. Exposed so a caller can draw a spectrogram, tune by ear, or assert a
 * sound's shape without synthesising it. */
double   wb_audio_sound_freq0(wb_snd s);
double   wb_audio_sound_freq1(wb_snd s);
double   wb_audio_sound_dur(wb_snd s);
double   wb_audio_sound_amp(wb_snd s);
int      wb_audio_sound_wave(wb_snd s);

#ifdef __cplusplus
}
#endif

#endif /* WB_AUDIO_H */
