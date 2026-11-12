/*
 * wb_lua_host.c -- WuBu Engine standalone entrypoint.
 *
 *   wubu_engine.exe <modes-root> <gametype> [player-id]
 *
 * Opens the world space, registers every gametype under <modes-root>, runs the
 * named mode's entry script with its space bindings installed, spawns the
 * player into that mode's world, and steps until the mode sets
 * wb.running = false.
 *
 * Build: gcc -std=c11 -Ithirdparty/lua5.4 -Isrc/core -Isrc/lua \
 *        src/core/wb_ecs.c src/core/wb_spatial.c src/core/wb_tiles.c \
 *        src/core/wb_render.c src/core/wb_exp.c src/core/wb_host.c \
 *        src/lua/wb_script.c src/lua/wb_space_bind.c src/lua/wb_lua_host.c \
 *        every .c under thirdparty/lua5.4 -o wubu_engine -lm
 */
#include "wb_host.h"
#include "wb_script.h"

#include <stdio.h>

static void err(const char *msg) { fprintf(stderr, "wb: %s\n", msg); }

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <modes-root> <gametype> [player-id]\n", argv[0]);
        return 2;
    }
    const char *root   = argv[1];
    const char *mode   = argv[2];
    const char *player = argc > 3 ? argv[3] : "player-1";

    char why[1024];
    wb_host *h = wb_host_open(root, mode, player, -1, why, sizeof why);
    if (!h) { err(why); return 1; }

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
    wb_host_close(h);
    printf("wb: engine exited clean after %u ticks (digest %016llx)\n",
           ticks, digest);
    if (have_frame)
        printf("wb: frame %.3f ms mean, %.3f ms p95, %.3f ms max "
               "(%lu overruns)\n",
               frame_stat.mean * 1000.0f, frame_stat.p95 * 1000.0f,
               frame_stat.mmax * 1000.0f, overruns);
    return 0;
}
