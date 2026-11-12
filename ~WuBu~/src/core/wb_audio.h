/*
 * wb_audio.h -- WuBu Engine audio: a synthesiser and a mixer.
 *
 * The floor, not the studio. There is no asset format, no device and no
 * streaming: a sound is a small set of parameters, a voice is a phase and a
 * clock, and mixing is a sum. That is enough to make a game AUDIBLE -- a footfall
 * per stride, a tone when the score changes -- without a content pipeline, which
 * is the same reason wb_walk exists before the animation clips do.
 *
 * What it deliberately is not: it does not play out of a device, does not stream,
 * does not do 3D attenuation or Doppler, and has no sample formats. A caller that
 * wants to hear it writes the mix to a file (the engine writes a WAV); a caller
 * with a device hands the same float buffer to it.
 *
 * C11, no external deps.
 */
#ifndef WB_AUDIO_H
#define WB_AUDIO_H

#include <stdint.h>

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
    int     active;
    wb_snd  sound;
    double  t;            /* seconds into the voice */
} wb_voice;

typedef struct {
    int      rate;                        /* samples per second */
    wb_voice voices[WB_AUDIO_MAX_VOICES];
    uint64_t emitted;                     /* events ever accepted */
    uint64_t dropped;                     /* events refused: mixer was full */
} wb_audio;

/* `rate` must be positive. 24000 divides evenly by the 60 Hz fixed step (400
 * samples per tick), which keeps the audio clock locked to the simulation clock
 * instead of drifting against it. */
void     wb_audio_init(wb_audio *a, int rate);

/* Start a sound. Returns 0 on success, -1 when every voice is busy -- the event
 * is DROPPED rather than stealing a voice mid-note, so a loud moment cannot
 * silently truncate a sound that is still playing. A caller that cares counts
 * `dropped`. */
int      wb_audio_emit(wb_audio *a, wb_snd s);

uint64_t wb_audio_emitted(const wb_audio *a);
uint64_t wb_audio_dropped(const wb_audio *a);

/* Voices still sounding right now. */
int      wb_audio_active(const wb_audio *a);

/* Render `nframes` of mono samples in [-1,1] into `out`, advancing every voice.
 * `out` must have room for nframes floats. Silent input produces exact zeros. */
void     wb_audio_mix(wb_audio *a, float *out, int nframes);

/* The parameters of a sound: a glide from freq0 to freq1 over dur seconds at amp
 * peak. Exposed so a caller can draw a spectrogram, tune by ear, or assert a
 * sound's shape without synthesising it. */
double   wb_audio_sound_freq0(wb_snd s);
double   wb_audio_sound_freq1(wb_snd s);
double   wb_audio_sound_dur(wb_snd s);
double   wb_audio_sound_amp(wb_snd s);

#ifdef __cplusplus
}
#endif

#endif /* WB_AUDIO_H */
