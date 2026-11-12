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
#include "wb_script.h"

#include <stdio.h>
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
                "[--render out.ppm]\n", argv[0]);
        return 2;
    }
    const char *root   = argv[1];
    const char *mode   = argv[2];
    const char *player = argc > 3 ? argv[3] : "player-1";

    /* The optional action to hold down for the whole run. */
    const char *input_name = "none";
    for (int i = 4; i < argc - 1; i++) {
        if (strcmp(argv[i], "--input") == 0) input_name = argv[i + 1];
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

    /* An optional path to write the last frame to, as a PPM. This is how the
     * headless binary shows what it rendered: there is no window to look at. */
    const char *render_path = NULL;
    for (int i = 4; i < argc - 1; i++) {
        if (strcmp(argv[i], "--render") == 0) render_path = argv[i + 1];
    }

    printf("wb: in '%s' as %s (instance %s)\n",
           wb_host_mode(h), player, wb_host_instance(h));

    int guard = 0;
    while (guard++ < 120000 && wb_host_running(h)) {
        if (wb_host_step(h, (float)WB_HOST_FIXED_DT) < 0) {
            const char *e = wb_host_error(h);
            err(e ? e : "mode step failed");
            break;
        }
    }

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
    wb_host_close(h);
    printf("wb: engine exited clean after %u ticks (digest %016llx)\n",
           ticks, digest);
    if (have_pos)
        printf("wb: %s at (%.2f, %.2f, %.2f)\n", player, p[0], p[1], p[2]);
    if (have_frame)
        printf("wb: frame %.3f ms mean, %.3f ms p95, %.3f ms max "
               "(%lu overruns)\n",
               frame_stat.mean * 1000.0f, frame_stat.p95 * 1000.0f,
               frame_stat.mmax * 1000.0f, overruns);
    return 0;
}
