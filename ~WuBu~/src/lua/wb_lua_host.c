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

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <modes-root> <gametype> [player-id] "
                "[--input fwd|back|left|right|jump|sprint|none] "
                "[--render out.ppm] [--sound out.wav] [--window] [--frames N]\n"
                "  --window   open a window and drive it with the keyboard/mouse\n"
                "  --frames N run at most N frames, then exit (with --window)\n"
                "  --sound F  write the match's audio to a 16-bit mono WAV\n",
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
    int use_window = 0, max_frames = 0, player_set = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_name = argv[++i];
        } else if (strcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            render_path = argv[++i];
        } else if (strcmp(argv[i], "--sound") == 0 && i + 1 < argc) {
            sound_path = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--window") == 0) {
            use_window = 1;
        } else if (!player_set) {
            player = argv[i];
            player_set = 1;
        }
    }

    char why[1024];
    wb_host *h = wb_host_open(root, mode, player, -1, why, sizeof why);
    if (!h) { err(why); return 1; }

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
        size_t nframes = 0;
        const float *snd = wb_host_audio(h, &nframes);
        /* wb_wav_write is the Big Mac writer and takes doubles; the host mixes
         * in floats, so the conversion happens here rather than in the writer. */
        double *pcm = NULL;
        if (nframes > 0) {
            pcm = malloc(nframes * sizeof(double));
            if (pcm) for (size_t i = 0; i < nframes; i++) pcm[i] = snd[i];
        }
        if (pcm && wb_wav_write(sound_path, pcm, nframes,
                                wb_host_audio_rate(h)) == 0) {
            /* The engine measures its own output rather than trusting the
             * synthesiser: the level of the whole mix, and the dominant
             * frequency of the loudest window. A footfall is a LOW thud, so the
             * peak is checked, not just the loudness. */
            double rms = 0.0;
            for (size_t i = 0; i < nframes; i++) rms += pcm[i] * pcm[i];
            rms = sqrt(rms / (double)(nframes ? nframes : 1));

            double best = 0.0, peak_hz = 0.0;
            size_t win = 4096;
            if (nframes >= win) {
                double *Re = malloc(win * sizeof(double));
                double *Im = malloc(win * sizeof(double));
                if (Re && Im) {
                    for (size_t off = 0; off + win <= nframes; off += win) {
                        double a = 0.0;
                        for (size_t i = 0; i < win; i++) {
                            double v = pcm[off + i];
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
        } else if (!pcm) {
            fprintf(stderr, "wb: cannot write '%s'\n", sound_path);
        }
        free(pcm);
    }

    wb_host_close(h);
    if (win) wb_win32_close(win);
    printf("wb: engine exited clean after %u ticks (digest %016llx)\n",
           ticks, digest);
    if (wb_host_objective(h))
        printf("wb: objective %s, score %u-%u, %.1fs left\n",
               wb_host_objective_state_name(h),
               wb_host_objective_score_of(h, 0),
               wb_host_objective_score_of(h, 1),
               wb_host_objective_clock(h));
    if (have_pos)
        printf("wb: %s at (%.2f, %.2f, %.2f)\n", player, p[0], p[1], p[2]);
    if (have_frame)
        printf("wb: frame %.3f ms mean, %.3f ms p95, %.3f ms max "
               "(%lu overruns)\n",
               frame_stat.mean * 1000.0f, frame_stat.p95 * 1000.0f,
               frame_stat.mmax * 1000.0f, overruns);
    return 0;
}
