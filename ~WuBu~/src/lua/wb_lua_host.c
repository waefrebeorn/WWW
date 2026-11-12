/*
 * wb_lua_host.c -- WuBu Engine standalone entrypoint.
 *
 *   wubu_engine.exe <modes-root> <gametype> [player-id] [--input <action>]
 *
 * Opens the world space, registers every gametype under <modes-root>, runs the
 * named mode's entry script with its space bindings installed, spawns the
 * player into that mode's world, and steps until the mode sets
 * wb.running = false.
 *
 * `--input` holds one action down for the whole run, which is how the headless
 * binary demonstrates the input -> controller -> velocity path that a windowed
 * platform layer drives with real keys. `none` (the default) leaves the input
 * state empty, so the mode's own script drives the player.
 *
 * Build: mingw32-make all
 */
#include "wb_host.h"
#include "wb_wav.h"
#include "wb_dsp.h"
#include "wb_script.h"
#include "wb_win32.h"

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void err(const char *msg) { fprintf(stderr, "wb: %s\n", msg); }

/* Write the framebuffer as a binary PPM (P6). No library, no dependency: it is
 * the one image format that can be written in six lines of C. */
static int write_ppm(const char *path, const wb_framebuffer *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fprintf(f, "P6\n%d %d\n255\n", fb->width, fb->height) < 0) {
        fclose(f);
        return -1;
    }
    for (int i = 0; i < fb->width * fb->height; i++) {
        uint32_t c = fb->pixels[i];
        unsigned char rgb[3] = {
            (unsigned char)((c >> 16) & 0xFF),
            (unsigned char)((c >> 8) & 0xFF),
            (unsigned char)(c & 0xFF)
        };
        if (fwrite(rgb, 1, 3, f) != 3) { fclose(f); return -1; }
    }
    return fclose(f) == 0 ? 0 : -1;
}

/* LP-87: fuzz the input with a seeded pseudo-random action stream, then RE-RUN
 * the log and compare. Both runs must agree; if they do not, the log is saved so
 * the failure can be reproduced rather than merely reported. The same seed always
 * produces the same stream, which is what makes that possible -- a fuzzer on a
 * deterministic engine is only useful if the failing case is replayable. */
static int run_fuzz(const char *root, const char *mode, const char *player,
                    uint64_t seed, int frames, const char *log_path) {
    char why[1024];
    wb_replay log;
    if (wb_replay_init(&log, (uint32_t)frames + 16) != 0) {
        err("cannot start a replay log");
        return 1;
    }

    wb_host *h = wb_host_open(root, mode, player, -1, why, sizeof why);
    if (!h) { err(why); wb_replay_free(&log); return 1; }
    wb_host_fuzz_start(h, seed, &log);
    for (int i = 0; i < frames; i++) {
        if (!wb_host_running(h)) break;
        if (wb_host_step(h, (float)WB_HOST_FIXED_DT) < 0) break;
    }
    wb_host_close(h);
    if (log.n == 0) {
        err("the fuzz run recorded nothing");
        wb_replay_free(&log);
        return 1;
    }

    h = wb_host_open(root, mode, player, -1, why, sizeof why);
    if (!h) { err(why); wb_replay_free(&log); return 1; }
    wb_host_replay_start(h, &log);
    for (uint32_t i = 0; i < log.n; i++)
        if (wb_host_step(h, (float)WB_HOST_FIXED_DT) < 0) break;
    int diverged = wb_host_replay_diverged(h);
    uint32_t tick = wb_host_replay_divergence_tick(h);
    wb_host_close(h);

    if (diverged) {
        printf("wb: seed %llu DIVERGED at tick %u (%u steps recorded)\n",
               (unsigned long long)seed, tick, log.n);
        if (log_path && wb_replay_save(&log, log_path) == 0)
            printf("wb: the failing log is at %s\n", log_path);
        wb_replay_free(&log);
        return 1;
    }
    printf("wb: seed %llu reproduced (%u steps, crc %08x)\n",
           (unsigned long long)seed, log.n, wb_replay_crc(&log));
    wb_replay_free(&log);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <modes-root> <gametype> [player-id] "
                "[--input fwd|back|left|right|jump|sprint|none] "
                "[--render out.ppm] [--sound out.wav] [--window] [--frames N] "
                "[--record log.wbr] [--replay log.wbr] [--fuzz seed]\n"
                "  --window   open a window and drive it with the keyboard/mouse\n"
                "  --frames N run at most N frames, then exit (with --window)\n"
                "  --sound F  write the match's audio to a 16-bit stereo WAV\n"
                "  --record F record every step's actions and digest to a replay log\n"
                "  --replay F re-run a recorded log and report the first divergent tick\n"
                "  --fuzz S   drive the input with a seeded random action stream, then\n"
                "             re-run the log and report whether it reproduced\n",
                argv[0]);
        return 2;
    }
    const char *root   = argv[1];
    const char *mode   = argv[2];
    /* Options start with `--`. The player id is the first argument that is not
     * one, and not the value of one -- so `--input fwd` does not make "fwd" the
     * player and `--window` does not either. */
    const char *player = "player-1";
    const char *input_name = "none";
    const char *render_path = NULL;
    const char *sound_path  = NULL;
    const char *record_path = NULL;
    const char *replay_path  = NULL;
    long long   fuzz_seed    = -1;
    int use_window = 0, max_frames = 0, player_set = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_name = argv[++i];
        } else if (strcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            render_path = argv[++i];
        } else if (strcmp(argv[i], "--sound") == 0 && i + 1 < argc) {
            sound_path = argv[++i];
        } else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            record_path = argv[++i];
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replay_path = argv[++i];
        } else if (strcmp(argv[i], "--fuzz") == 0 && i + 1 < argc) {
            fuzz_seed = strtoll(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--window") == 0) {
            use_window = 1;
        } else if (!player_set) {
            player = argv[i];
            player_set = 1;
        }
    }

    if (fuzz_seed >= 0) {
        int frames = max_frames > 0 ? max_frames : 600;
        return run_fuzz(root, mode, player, (uint64_t)fuzz_seed, frames,
                        record_path);
    }

    char why[1024];
    if (record_path && replay_path) {
        err("--record and --replay are mutually exclusive");
        return 2;
    }
    wb_replay log;
    memset(&log, 0, sizeof log);
    if (replay_path) {
        if (wb_replay_load(&log, replay_path) != 0) {
            err("cannot load the replay log");
            return 1;
        }
    } else if (record_path) {
        if (wb_replay_init(&log, 4096) != 0) {
            err("cannot start a replay log");
            return 1;
        }
    }

    wb_host *h = wb_host_open(root, mode, player, -1, why, sizeof why);
    if (!h) { err(why); wb_replay_free(&log); return 1; }

    if (replay_path) {
        if (wb_host_replay_start(h, &log) != 0) {
            err("cannot start the replay");
            wb_host_close(h); wb_replay_free(&log);
            return 1;
        }
    } else if (record_path) {
        if (wb_host_record_start(h, &log) != 0) {
            err("cannot start recording");
            wb_host_close(h); wb_replay_free(&log);
            return 1;
        }
    }

    /* Translate the named action into the host's key map and hold it. */
    int input_code = -1;
    if      (strcmp(input_name, "fwd")    == 0) input_code = 'W';
    else if (strcmp(input_name, "back")   == 0) input_code = 'S';
    else if (strcmp(input_name, "left")   == 0) input_code = 'A';
    else if (strcmp(input_name, "right")  == 0) input_code = 'D';
    else if (strcmp(input_name, "jump")   == 0) input_code = ' ';
    else if (strcmp(input_name, "sprint") == 0) input_code = 0x10;
    else if (strcmp(input_name, "none")   != 0) {
        fprintf(stderr, "wb: unknown --input '%s' "
                        "(fwd|back|left|right|jump|sprint|none)\n", input_name);
        return 2;
    }
    if (input_code >= 0) wb_host_input_key_down(h, input_code);

    /* The window is the platform layer: its framebuffer is what gets presented,
     * and its keyboard/mouse events go straight into the host's input state, so
     * a real person drives the same path `--input` drives headlessly. */
    wb_win32 *win = NULL;
    wb_framebuffer *wfb = NULL;
    if (use_window) {
        win = wb_win32_open(960, 600, "WuBu Engine", wb_host_input(h),
                            why, sizeof why);
        if (!win) {
            err(why);
            wb_host_close(h);
            return 1;
        }
        wfb = wb_win32_framebuffer(win);
        printf("wb: window %dx%d\n", wb_win32_width(win), wb_win32_height(win));
    }

    printf("wb: in '%s' as %s (instance %s)\n",
           wb_host_mode(h), player, wb_host_instance(h));

    int guard = 0, frames = 0;
    while (guard++ < 120000 && wb_host_running(h)) {
        if (win && wb_win32_pump(win) == 0) break;
        if (wb_host_step(h, (float)WB_HOST_FIXED_DT) < 0) {
            const char *e = wb_host_error(h);
            err(e ? e : "mode step failed");
            break;
        }
        if (win) {
            wb_host_render(h, wfb);
            wb_win32_present(win);
        }
        frames++;
        if (max_frames > 0 && frames >= max_frames) break;
    }
    if (win) printf("wb: window closed after %d frames\n", frames);

    /* Physics is integrated by the host, once per fixed step, so the caller only
     * paces frames. */
    uint32_t ticks = wb_host_ticks(h);
    unsigned long long digest = (unsigned long long)wb_host_digest(h);
    wb_prof_stat frame_stat;
    int have_frame = (wb_host_prof_summary(h, "frame", &frame_stat) == 0);
    unsigned long overruns = wb_host_prof_overruns(h);
    double p[3] = {0, 0, 0};
    int have_pos = (wb_host_player_pos(h, p) == 0);

    int drawn = -1;
    if (render_path) {
        static uint32_t pixels[640 * 480];
        wb_framebuffer fb = { .pixels = pixels, .width = 640, .height = 480 };
        drawn = wb_host_render(h, &fb);
        if (drawn < 0) {
            err("cannot render the frame");
        } else if (write_ppm(render_path, &fb) != 0) {
            fprintf(stderr, "wb: cannot write '%s'\n", render_path);
        } else {
            printf("wb: rendered %d marker(s) to %s\n", drawn, render_path);
        }
    }
    /* The match's audio, written out as a file the space owns rather than played
     * through a device: the same rule as the PPM frames. */
    if (sound_path) {
        const float *snd_l = NULL, *snd_r = NULL;
        size_t nframes = wb_host_audio(h, &snd_l, &snd_r);
        /* wb_wav_write_stereo is the Big Mac writer and takes doubles; the host
         * mixes in floats, so the conversion happens here rather than in the
         * writer. */
        double *pcm_l = NULL, *pcm_r = NULL;
        if (nframes > 0 && snd_l && snd_r) {
            pcm_l = malloc(nframes * sizeof(double));
            pcm_r = malloc(nframes * sizeof(double));
            if (pcm_l && pcm_r) {
                for (size_t i = 0; i < nframes; i++) {
                    pcm_l[i] = snd_l[i];
                    pcm_r[i] = snd_r[i];
                }
            }
        }
        if (pcm_l && pcm_r &&
            wb_wav_write_stereo(sound_path, pcm_l, pcm_r, nframes,
                                wb_host_audio_rate(h)) == 0) {
            /* The engine measures its own output rather than trusting the
             * synthesiser: the level of the whole mix, and the dominant
             * frequency of the loudest window. A footfall is a LOW thud, so the
             * peak is checked, not just the loudness. */
            double rms = 0.0;
            for (size_t i = 0; i < nframes; i++)
                rms += pcm_l[i] * pcm_l[i] + pcm_r[i] * pcm_r[i];
            rms = sqrt(rms / (double)(2 * (nframes ? nframes : 1)));

            double best = 0.0, peak_hz = 0.0;
            size_t win = 4096;
            if (nframes >= win) {
                double *Re = malloc(win * sizeof(double));
                double *Im = malloc(win * sizeof(double));
                if (Re && Im) {
                    for (size_t off = 0; off + win <= nframes; off += win) {
                        double a = 0.0;
                        for (size_t i = 0; i < win; i++) {
                            double v = pcm_l[off + i];
                            Re[i] = v; Im[i] = 0.0;
                            a += v * v;
                        }
                        if (sqrt(a / (double)win) <= best) continue;
                        best = sqrt(a / (double)win);
                        if (wb_rfft(Re, win, Re, Im) == 0) {
                            size_t bin = wb_argmax_abs(Re, 1, win / 2);
                            peak_hz = (double)bin *
                                      (double)wb_host_audio_rate(h) /
                                      (double)win;
                        }
                    }
                }
                free(Re); free(Im);
            }
            printf("wb: wrote %zu samples to %s (%llu events, rms %.4f, "
                   "peak %.0f Hz in the loudest window)\n",
                   nframes, sound_path,
                   (unsigned long long)wb_host_audio_events(h),
                   rms, peak_hz);
        } else if (!pcm_l || !pcm_r) {
            fprintf(stderr, "wb: cannot write '%s'\n", sound_path);
        }
        free(pcm_l); free(pcm_r);
    }

    /* The replay verdict. The host is still alive here: the divergence is its
     * answer, and it has to be read before the close. */
    if (record_path) {
        if (wb_replay_save(&log, record_path) == 0)
            printf("wb: recorded %u steps to %s (crc %08x)\n",
                   log.n, record_path, wb_replay_crc(&log));
        else
            fprintf(stderr, "wb: cannot write '%s'\n", record_path);
    }
    if (replay_path) {
        if (wb_host_replay_diverged(h))
            printf("wb: REPLAY DIVERGED at tick %u\n",
                   wb_host_replay_divergence_tick(h));
        else
            printf("wb: replay reproduced the run (%u steps, crc %08x)\n",
                   log.n, wb_replay_crc(&log));
    }
    wb_replay_free(&log);

    /* The objective is read BEFORE the close: afterwards the host is gone. */
    int have_obj = (wb_host_objective(h) != NULL);
    char obj_name[64] = "none";
    unsigned obj_score[2] = { 0, 0 };
    double obj_clock = 0.0;
    if (have_obj) {
        snprintf(obj_name, sizeof obj_name, "%s",
                 wb_host_objective_state_name(h));
        obj_score[0] = wb_host_objective_score_of(h, 0);
        obj_score[1] = wb_host_objective_score_of(h, 1);
        obj_clock = wb_host_objective_clock(h);
    }

    wb_host_close(h);
    if (win) wb_win32_close(win);
    printf("wb: engine exited clean after %u ticks (digest %016llx)\n",
           ticks, digest);
    if (have_obj)
        printf("wb: objective %s, score %u-%u, %.1fs left\n",
               obj_name, obj_score[0], obj_score[1], obj_clock);
    if (have_pos)
        printf("wb: %s at (%.2f, %.2f, %.2f)\n", player, p[0], p[1], p[2]);
    if (have_frame)
        printf("wb: frame %.3f ms mean, %.3f ms p95, %.3f ms max "
               "(%lu overruns)\n",
               frame_stat.mean * 1000.0f, frame_stat.p95 * 1000.0f,
               frame_stat.mmax * 1000.0f, overruns);
    return 0;
}
