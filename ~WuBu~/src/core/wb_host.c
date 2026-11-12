/*
 * wb_host.c -- the gametype host (slice 22). See wb_host.h.
 */
#include "wb_host.h"
#include "wb_character.h"
#include "wb_det.h"
#include "wb_hud.h"
#include "wb_input.h"
#include "wb_interp.h"
#include "wb_prof.h"
#include "wb_script.h"
#include "wb_snap.h"
#include "wb_space_bind.h"
#include "wb_worldspace.h"
#include "wb_util.h"
#include "wb_walk.h"
#include "wb_audio.h"

#include <lua.h>
#include <lauxlib.h>

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The audio rate of the shipped host. Chosen to divide evenly by the 60 Hz fixed
 * step -- 400 samples per tick -- so audio is a whole number of samples per
 * simulation step and the two clocks cannot drift apart. */
#define WB_AUDIO_RATE 24000

struct wb_host {
    wb_worldspace *ws;
    wb_world      *world;
    lua_State     *L;
    wb_entity      player;      /* the player's body in the mode's world */
    wb_det_fixed   acc;         /* fixed-step accumulator (LP-03/LP-04) */
    uint64_t       digest;      /* per-tick divergence digest (LP-03) */
    uint32_t       ticks;       /* whole sim steps taken */
    float          alpha;       /* render-interpolation alpha in [0,1) */
    wb_prof       *prof;        /* frame profiler (LP-01/LP-05) */
    wb_input       in;          /* platform-agnostic input state */
    wb_camera      cam;         /* follow camera */
    wb_character   ch;          /* player controller */
    wb_character_body body;     /* the player's body, over the ECS */
    wb_host_keys   keys;        /* key map */
    wb_objective   objective;   /* the match objective (LP-114), if any */
    int            has_objective;
    char           hint[WB_MODE_CFG_VALUE];  /* onboarding line, from mode.json */
    double         hint_seconds;             /* how long it stays up, 0 = never */
    wb_walk       *walks;       /* per-entity walk cycle, indexed by entity index */
    uint32_t       nwalks;      /* allocated entries in `walks` */
    int           *last_step;   /* per-entity last half-cycle index (footsteps) */
    wb_audio       audio;       /* the sound of the match (LP-42 stone 2) */
    uint32_t       last_score;  /* the score last sounded for */
    /* The accumulated STEREO mix. One allocation, two halves: the channels must
     * always be the same length, and a partial grow would leave the shorter one
     * to be overwritten by the next mix. `snd_r` is `snd_l + snd_cap`. */
    float         *snd_l;       /* accumulated left channel, for the caller to write */
    float         *snd_r;       /* accumulated right channel (aliases snd_l + cap) */
    size_t         snd_n;       /* frames accumulated */
    size_t         snd_cap;     /* allocated frames per channel */
    /* The PREVIOUS step's positions, for render interpolation (LP-04): where each
     * body was before the last fixed step. The renderer lerps from these to the
     * current ones by the accumulator's remainder, so a 60 Hz sim on a faster
     * display glides instead of juddering. `prev_ok` marks a body with no state
     * last step (spawned, or dead): it is drawn where it is rather than
     * interpolated out of a stale slot.
     *
     * The walk PHASE is deliberately not interpolated. It is stride state, not a
     * transform, and the stride is a sine: lerping it shifts which part of the
     * cycle two frames land on, which makes the legs read as juddering even when
     * they are not. A body glides; its stride keeps its own clock. */
    float  *prev_pos;            /* 3 per entity, world z-up */
    uint8_t *prev_ok;            /* 1 per entity */
    uint32_t prev_n;             /* allocated entries */
    /* Replay (LP-87). The log is borrowed, never owned: `rec_log` is being
     * written, `replay_log` is being re-run, never both. `replay_actions` is what
     * the controller applied for the current step -- the live input while
     * recording, the recorded actions while replaying. */
    wb_replay       *rec_log;
    const wb_replay *replay_log;
    wb_actions      replay_actions;
    int             replaying;
    int             diverged;
    uint32_t        div_tick;
    char mode[WB_MODE_NAME_LEN];
    char instance[WB_DIST_ID_LEN];
    char err[1024];
};

/* The components that ARE the simulation state. Everything else (mass, label)
 * is either constant or cosmetic, so the digest covers exactly what a replay,
 * a rollback or a mesh authority would compare. */
static const wb_comp_type wb_host_state[] = { WB_COMP_POSITION, WB_COMP_VELOCITY };

/* ---- The player's body, expressed over the ECS --------------------------- */

typedef struct {
    wb_world *w;
    uint32_t  index;
} wb_ecs_body_ctx;

static float *wb_ecs_comp(wb_ecs_body_ctx *c, wb_comp_type t) {
    if (!wb_world_slot_alive(c->w, c->index)) return NULL;
    if (!wb_world_comp_present(c->w, t, c->index)) return NULL;
    size_t esz = wb_world_comp_elem_size(c->w, t);
    uint8_t *base = (uint8_t *)wb_world_comp_data(c->w, t);
    return (float *)(base + (size_t)c->index * esz);
}

static void wb_ecs_get_vel(void *ctx, double v[3]) {
    wb_host *h = (wb_host *)ctx;
    wb_ecs_body_ctx c = { h->world, h->player.index };
    float *f = wb_ecs_comp(&c, WB_COMP_VELOCITY);
    v[0] = f ? f[0] : 0.0;
    v[1] = f ? f[1] : 0.0;
    v[2] = f ? f[2] : 0.0;
}
static void wb_ecs_set_vel(void *ctx, const double v[3]) {
    wb_host *h = (wb_host *)ctx;
    wb_ecs_body_ctx c = { h->world, h->player.index };
    float *f = wb_ecs_comp(&c, WB_COMP_VELOCITY);
    if (f) { f[0] = (float)v[0]; f[1] = (float)v[1]; f[2] = (float)v[2]; }
}
static void wb_ecs_set_yaw(void *ctx, double yaw) {
    /* The ECS has no facing component; the controller's own yaw is authoritative
     * for anything that asks it back. */
    (void)ctx; (void)yaw;
}

const char *wb_host_error(const wb_host *h) {
    return (h && h->err[0]) ? h->err : NULL;
}
const char *wb_host_mode(const wb_host *h)   { return h ? h->mode : NULL; }
const char *wb_host_instance(const wb_host *h) { return h ? h->instance : NULL; }
wb_world *wb_host_world(wb_host *h)         { return h ? h->world : NULL; }
wb_worldspace *wb_host_space(wb_host *h)    { return h ? h->ws : NULL; }
wb_entity wb_host_player(const wb_host *h)  { return h ? h->player : (wb_entity){0, 0}; }

/* Register every immediate subdirectory of `root` that holds a mode.json.
 * Returns the number registered, or -1 on the first failure. */
static int register_modes_in(wb_worldspace *ws, const char *root,
                             char *err, size_t err_sz) {
    DIR *d = opendir(root);
    if (!d) {
        snprintf(err, err_sz, "cannot open gametypes directory %s", root);
        return -1;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;          /* . .. and dotfiles */
        char dir[800];
        wb_join_path(dir, sizeof dir, root, e->d_name);
        char manifest[900];
        wb_join_path(manifest, sizeof manifest, dir, "mode.json");
        if (!wb_file_exists(manifest)) continue;    /* not a gametype */

        if (wb_worldspace_register_mode(ws, dir, err, err_sz) != 0) {
            closedir(d);
            return -1;
        }
        /* Install the mode's DECLARED spawn points. These must exist before any
         * script runs, or a mode could not be travelled into. */
        const wb_mode_manifest *m = wb_worldspace_mode_at(ws, n);
        for (int i = 0; m && i < m->nspawns; i++) {
            wb_worldspace_add_spawn(ws, m->name, m->spawns[i].pos,
                                    m->spawns[i].team, m->spawns[i].tag);
        }
        n++;
    }
    closedir(d);
    return n;
}

wb_host *wb_host_open(const char *modes_root, const char *gamemode,
                      const char *player_id, int team,
                      char *err, size_t err_sz) {
    if (!modes_root || !gamemode || !gamemode[0]) {
        if (err) snprintf(err, err_sz, "a modes directory and a gametype are required");
        return NULL;
    }

    wb_host *h = (wb_host *)calloc(1, sizeof *h);
    if (!h) {
        if (err) snprintf(err, err_sz, "out of memory");
        return NULL;
    }

    h->ws = wb_worldspace_open();
    if (!h->ws) {
        snprintf(h->err, sizeof h->err, "cannot open the world space");
        goto fail;
    }

    int n = register_modes_in(h->ws, modes_root, h->err, sizeof h->err);
    if (n < 0) goto fail;
    if (n == 0) {
        snprintf(h->err, sizeof h->err,
                 "no gametypes under %s (each needs a mode.json)", modes_root);
        goto fail;
    }
    if (!wb_worldspace_mode(h->ws, gamemode)) {
        snprintf(h->err, sizeof h->err,
                 "gametype '%s' is not one of the %d registered under %s",
                 gamemode, n, modes_root);
        goto fail;
    }

    /* The world this mode runs in. */
    h->world = wb_world_new();
    if (!h->world) {
        snprintf(h->err, sizeof h->err, "out of memory");
        goto fail;
    }
    wb_world_setup_comps(h->world);
    wb_det_fixed_init(&h->acc, WB_HOST_FIXED_DT);

    /* The profiler: every host step is inside a frame zone, and the frame zone
     * carries the budget the budget gate (LP-05) measures against. */
    h->prof = wb_prof_new(256);
    if (!h->prof) {
        snprintf(h->err, sizeof h->err, "out of memory");
        goto fail;
    }
    wb_prof_set_budget(h->prof, "frame", (float)WB_HOST_FIXED_DT);

    /* Input, camera and the player controller. The default key map is WASD +
     * Space + Shift in platform key codes; a platform layer remaps it. */
    wb_input_init(&h->in);
    wb_camera_init(&h->cam);
    wb_character_init(&h->ch);
    /* 24000 Hz divides evenly by the 60 Hz fixed step (400 samples per tick), so
     * the audio clock locks to the simulation clock instead of drifting. */
    wb_audio_init(&h->audio, WB_AUDIO_RATE);
    h->keys.fwd = 'W'; h->keys.back = 'S';
    h->keys.left = 'A'; h->keys.right = 'D';
    h->keys.jump = ' '; h->keys.sprint = 0x10;   /* Shift */
    h->body.ctx     = h;
    h->body.get_vel = wb_ecs_get_vel;
    h->body.set_vel = wb_ecs_set_vel;
    h->body.grounded = NULL;      /* the ECS has no ground plane */
    h->body.set_yaw = wb_ecs_set_yaw;

    /* The mode's script, bound to its world and carrying its space context --
     * so wb.add_spawn / wb.add_gate land in the mode's OWN world. */
    h->L = wb_script_create_state(h->world, h->ws, gamemode);
    if (!h->L) {
        snprintf(h->err, sizeof h->err, "cannot create the mode's Lua state");
        goto fail;
    }
    if (wb_space_bind_install(h->L) != 0) {
        snprintf(h->err, sizeof h->err, "cannot install the space bindings");
        goto fail;
    }
    /* The objective and config bindings go in BEFORE the entry script runs: a
     * mode reads its own tuning at load time, and it would otherwise see a nil
     * wb.cfg. */
    wb_host_install_bindings(h->L, h);

    /* Run the entry script FIRST: it declares the mode's spawns and stadiums,
     * and only then can a player be put into the world. */
    char mode_dir[600], entry[800];
    if (wb_worldspace_mode_dir(h->ws, gamemode, mode_dir, sizeof mode_dir) != 0 ||
        wb_mode_entry_path(wb_worldspace_mode(h->ws, gamemode), mode_dir,
                           entry, sizeof entry) != 0) {
        snprintf(h->err, sizeof h->err, "cannot resolve %s's entry script", gamemode);
        goto fail;
    }
    if (luaL_dofile(h->L, entry) != LUA_OK) {
        snprintf(h->err, sizeof h->err, "%s: %s", gamemode, lua_tostring(h->L, -1));
        lua_pop(h->L, 1);
        goto fail;
    }

    /* NOW spawn the player, at a point the mode itself declared. */
    float pos[3];
    if (wb_worldspace_spawn(h->ws, gamemode, NULL, player_id, team,
                            h->instance, pos, h->err, sizeof h->err) != 0) {
        goto fail;
    }

    /* Materialize them as a body in the mode's world -- the space picked WHERE,
     * this puts an entity THERE -- and hand the handle to the script as
     * wb.player, so the mode can see and move them. */
    h->player = wb_world_spawn_entity(h->world, player_id, pos);
    lua_getglobal(h->L, "wb");
    if (lua_istable(h->L, -1)) {
        wb_script_push_entity(h->L, h->player);
        lua_setfield(h->L, -2, "player");
        lua_pushstring(h->L, player_id);
        lua_setfield(h->L, -2, "player_id");
        lua_pop(h->L, 1);
    } else {
        lua_pop(h->L, 1);
    }

    snprintf(h->mode, sizeof h->mode, "%s", gamemode);

    /* The objective comes from the mode's own manifest, through the config
     * schema like every other mode parameter -- data before scripts. A rule of
     * 0 (or a mode that declares no objective) means the mode runs until its
     * script says stop, exactly as before. */
    const wb_mode_manifest *mf = wb_worldspace_mode(h->ws, gamemode);
    if (mf) {
        wb_mode_config cfg;
        wb_mode_config_defaults(mf, &cfg);
        int rule = wb_mode_config_get_int(mf, &cfg, "objective_rule", 0);
        if (rule > 0) {
            uint32_t sides  = (uint32_t)wb_mode_config_get_int(
                                  mf, &cfg, "objective_sides", 2);
            uint32_t target = (uint32_t)wb_mode_config_get_int(
                                  mf, &cfg, "objective_target", 3);
            double limit = wb_mode_config_get_float(
                               mf, &cfg, "objective_time_limit", 60.0);
            if (wb_host_set_objective(h, rule, sides, target, limit) != 0) {
                snprintf(h->err, sizeof h->err, "%s", "bad objective");
                goto fail;
            }
        }
        /* Onboarding: the mode's own hint line and how long it stays up. Data,
         * like the objective -- a mode is told what to teach, not how. */
        snprintf(h->hint, sizeof h->hint, "%s",
                 wb_mode_config_get_string(mf, &cfg, "hint", ""));
        h->hint_seconds = wb_mode_config_get_float(
                              mf, &cfg, "hint_seconds", 0.0);
    }

    h->err[0] = 0;
    return h;

fail:
    if (err) snprintf(err, err_sz, "%s", h->err);
    wb_host_close(h);
    return NULL;
}

void wb_host_close(wb_host *h) {
    if (!h) return;
    h->rec_log = NULL;                  /* borrowed: the caller owns the log */
    h->replay_log = NULL;
    if (h->L) lua_close(h->L);
    if (h->prof) wb_prof_dispose(h->prof);
    if (h->world) wb_world_destroy(h->world);
    if (h->ws) wb_worldspace_close(h->ws);
    free(h->walks);
    free(h->last_step);
    free(h->prev_pos);
    free(h->prev_ok);
    free(h->snd_l);                 /* snd_r is a half of the same allocation */
    wb_audio_free(&h->audio);
    free(h);
}

/* ---- Walk cycles --------------------------------------------------------- */

/* Grow the per-entity walk table to cover every entity index. Entities are
 * recycled, so a slot that has gone dead keeps its old phase; a respawned body
 * starting mid-stride is not a defect worth special-casing. */
static void wb_host_grow_walks(wb_host *h, uint32_t need) {
    if (need <= h->nwalks) return;
    uint32_t cap = h->nwalks ? h->nwalks : 16;
    while (cap < need) cap *= 2;
    wb_walk *grown = realloc(h->walks, (size_t)cap * sizeof *grown);
    if (!grown) return;                    /* no animation rather than no host */
    int *steps = realloc(h->last_step, (size_t)cap * sizeof *steps);
    if (!steps) { h->walks = grown; return; }  /* walks grow, footsteps do not */
    for (uint32_t i = h->nwalks; i < cap; i++)
        wb_walk_init(&grown[i], WB_WALK_RATE_PER_METRE);
    for (uint32_t i = h->nwalks; i < cap; i++) steps[i] = -1;
    h->walks = grown;
    h->last_step = steps;
    h->nwalks = cap;
}

/* Grow the previous-step tables. Three arrays rather than one packed block: a
 * partial growth leaves the arrays different lengths, and every accessor checks
 * `index < prev_n` before touching them, so the short ones are simply unreachable
 * rather than overwritten. */
static int wb_host_grow_prev(wb_host *h, uint32_t n) {
    if (n <= h->prev_n) return 0;
    float *pp = realloc(h->prev_pos, (size_t)n * 3 * sizeof(float));
    if (!pp) return -1;
    h->prev_pos = pp;
    uint8_t *ok = realloc(h->prev_ok, (size_t)n);
    if (!ok) return -1;
    h->prev_ok = ok;
    h->prev_n = n;
    return 0;
}

/* Remember where every body IS, before the step moves it. Called once per whole
 * fixed step, before the physics tick: the state captured here is the "previous"
 * end of the lerp, and the state after the tick is the "current" end. */
static void wb_host_snapshot_prev(wb_host *h) {
    uint32_t n = wb_world_next_index(h->world);
    if (n == 0) return;
    if (wb_host_grow_prev(h, n) != 0) return;   /* no interpolation this step */
    size_t esz = wb_world_comp_elem_size(h->world, WB_COMP_POSITION);
    uint8_t *base = (uint8_t *)wb_world_comp_data(h->world, WB_COMP_POSITION);
    for (uint32_t i = 0; i < n; i++) {
        h->prev_ok[i] = 0;
        if (base && wb_world_slot_alive(h->world, i) &&
            wb_world_comp_present(h->world, WB_COMP_POSITION, i)) {
            float *p = (float *)(base + (size_t)i * esz);
            float *dst = &h->prev_pos[(size_t)i * 3];
            dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
            h->prev_ok[i] = 1;
        }
    }
}

/* Advance every body's walk cycle by its OWN velocity, so a figure walks at the
 * speed it is actually moving. Runs once per whole sim step, never per frame:
 * the phase is simulation state, and a render must not be able to move it. */
static void wb_host_advance_walks(wb_host *h, double dt) {
    uint32_t n = wb_world_next_index(h->world);
    if (n == 0) return;
    wb_host_grow_walks(h, n);
    if (!h->walks) return;
    size_t esz = wb_world_comp_elem_size(h->world, WB_COMP_VELOCITY);
    uint8_t *vbase = (uint8_t *)wb_world_comp_data(h->world, WB_COMP_VELOCITY);
    for (uint32_t i = 0; i < n; i++) {
        double speed = 0.0;
        if (vbase && wb_world_slot_alive(h->world, i) &&
            wb_world_comp_present(h->world, WB_COMP_VELOCITY, i)) {
            float *v = (float *)(vbase + (size_t)i * esz);
            speed = sqrt((double)v[0]*v[0] + (double)v[1]*v[1] + (double)v[2]*v[2]);
        }
        wb_walk_step(&h->walks[i], dt, speed);
    }
}

/* A body drawn as a figure rather than a disc: torso, two legs, two arms.
 * EVERYTHING here is in the renderer's own space (y-up), which is what the
 * camera `rc` and the framebuffer use -- `pos` is a body position already mapped
 * from the world's z-up, and `right`/`forward` are the camera's axes mapped the
 * same way. Getting that wrong lays the figure down along the view axis.
 *
 * `pos` is the body's feet; `walk` drives both swing terms, so a standing body
 * stands. */
static void wb_host_draw_figure(wb_framebuffer *fb, const wb_render_camera *rc,
                                const float pos[3], const float right[3],
                                const float forward[3], const wb_walk *walk,
                                uint32_t color) {
    /* Figure proportions, metres. A 1.8 m body. */
    const double hip_h = 0.95, head_h = 1.75, shoulder_h = 1.45;
    const double s  = walk ? sin(walk->phase) : 0.0;    /* the stride       */
    const double lL = s > 0.0 ? s : 0.0;               /* left foot lifts  */
    const double lR = s < 0.0 ? -s : 0.0;              /* right foot lifts */
    const double bob = 0.05 * fabs(s);                 /* rises per stride */

    /* Lateral along the camera's right, fore/aft along its forward, +y is up. */
    #define PT(ox, oy, oz, out) do { \
        out[0] = (float)(pos[0] + right[0]*(ox) + forward[0]*(oy)); \
        out[1] = (float)(pos[1] + (oz)); \
        out[2] = (float)(pos[2] + right[2]*(ox) + forward[2]*(oy)); \
    } while (0)

    float hip[3], head[3], sh_l[3], sh_r[3];
    float foot_l[3], foot_r[3], hand_l[3], hand_r[3];
    PT(0.0,  0.0,        hip_h + bob, hip);
    PT(0.0,  0.0,        head_h + bob, head);
    PT(-0.20, 0.0,       shoulder_h + bob, sh_l);
    PT( 0.20, 0.0,       shoulder_h + bob, sh_r);
    PT(-0.16,  0.22 * s, 0.02 + 0.14 * lL, foot_l);
    PT( 0.16, -0.22 * s, 0.02 + 0.14 * lR, foot_r);
    PT(-0.24, -0.08 * s, 0.95 + bob, hand_l);
    PT( 0.24,  0.08 * s, 0.95 + bob, hand_r);
    #undef PT

    /* Limbs first, torso over them: a limb must never appear to pass in front of
     * the chest. The head goes on last of all. */
    wb_render_segment(fb, rc, hip, foot_l, 0.07f, color);          /* leg L    */
    wb_render_segment(fb, rc, hip, foot_r, 0.07f, color);          /* leg R    */
    wb_render_segment(fb, rc, sh_l, hand_l, 0.06f, color);         /* arm L    */
    wb_render_segment(fb, rc, sh_r, hand_r, 0.06f, color);         /* arm R    */
    wb_render_segment(fb, rc, sh_l, sh_r, 0.09f, color);          /* shoulders */
    wb_render_segment(fb, rc, hip, head, 0.10f, color);           /* torso    */
    wb_render_marker(fb, rc, head, 0.14f, color);                 /* head     */
}

/* How far a body can be heard. A stadium is big and the mixer has eight voices;
 * a footfall on the far side of the ground is not an event the player can use,
 * and letting it take a voice would push out the one beside them. */
#define WB_HOST_HEARING 30.0f

/* A body's position in the world, or -1 when it has none. */
static int wb_host_entity_pos(wb_host *h, uint32_t index, double p[3]) {
    if (!h || !h->world || !p) return -1;
    size_t esz = wb_world_comp_elem_size(h->world, WB_COMP_POSITION);
    uint8_t *base = (uint8_t *)wb_world_comp_data(h->world, WB_COMP_POSITION);
    if (!base || !wb_world_slot_alive(h->world, index) ||
        !wb_world_comp_present(h->world, WB_COMP_POSITION, index))
        return -1;
    float *f = (float *)(base + (size_t)index * esz);
    p[0] = f[0]; p[1] = f[1]; p[2] = f[2];
    return 0;
}

/* Where a body is HEARD from: direction and distance from the listener's ear,
 * in the listener's own frame. The camera is the listener, so looking somewhere
 * is what puts a sound on one side rather than in the middle. */
static void wb_host_placement(wb_host *h, uint32_t index,
                              float *az, float *el, float *dist) {
    double p[3], eye[3];
    *az = 0.0f; *el = 0.0f; *dist = 1.0f;
    if (wb_host_entity_pos(h, index, p) != 0) return;
    wb_camera_eye(&h->cam, eye);
    double dx = p[0] - eye[0], dy = p[1] - eye[1], dz = p[2] - eye[2];
    double ground = sqrt(dx * dx + dy * dy);
    double len = sqrt(ground * ground + dz * dz);
    *dist = (float)(len > 0.1 ? len : 0.1);
    /* The bearing is left in the WORLD frame (the camera looks along +x at yaw
     * 0). The listener's yaw is applied separately in the mixer, which turns the
     * head; subtracting it here too would turn it twice. */
    double deg = atan2(dy, dx) * 180.0 / 3.14159265358979;
    while (deg > 180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    *az = (float)deg;
    *el = (float)(atan2(dz, ground) * 180.0 / 3.14159265358979);
}

/* One body's footfall, at that body's place. A stopped body holds its pose, so
 * it holds its silence too. */
static void wb_host_footstep(wb_host *h, uint32_t index) {
    const wb_walk *w = &h->walks[index];
    if (w->speed <= 0.05) return;
    int half = (int)floor(w->phase / 3.14159265358979);
    if (half == h->last_step[index]) return;
    h->last_step[index] = half;
    float az, el, dist;
    wb_host_placement(h, index, &az, &el, &dist);
    if (dist > WB_HOST_HEARING) return;
    wb_audio_emit_at(&h->audio, WB_SND_FOOTSTEP, az, el, dist);
}

/* Turn simulation events into sound. Runs once per whole fixed step, so the
 * audio clock IS the simulation clock -- a frame that buys no step makes no
 * sound, and a match replays identically however the caller paces its frames. */
static void wb_host_emit_sounds(wb_host *h) {
    /* A footfall per half stride, for every walking body, each at its own place
     * in the world -- a footfall behind you is not the sound as one in front of
     * you. The player goes first: their own footfall must not be the event a
     * crowded ground drops. */
    if (h->walks && h->last_step) {
        uint32_t n = h->nwalks;
        if (h->player.index < n) wb_host_footstep(h, h->player.index);
        for (uint32_t i = 0; i < n; i++)
            if (i != h->player.index) wb_host_footstep(h, i);
    }
    /* A tone when the score changes, on any side. */
    if (h->has_objective) {
        uint32_t sides = wb_objective_sides(&h->objective);
        uint32_t sc = 0;
        for (uint32_t s = 0; s < sides; s++)
            sc += wb_objective_score_of(&h->objective, s);
        if (sc != h->last_score) {
            h->last_score = sc;
            if (sc > 0) wb_audio_emit(&h->audio, WB_SND_SCORE);
        }
    }
}

/* Mix `dt` seconds of audio and append it to the host's accumulated buffer.
 * The buffer grows; a failed growth drops the audio rather than the host. */
static void wb_host_mix_audio(wb_host *h, double dt) {
    int n = (int)(h->audio.rate * dt);
    if (n <= 0) return;
    if (h->snd_n + (size_t)n > h->snd_cap) {
        size_t cap = h->snd_cap ? h->snd_cap : 4096;
        while (cap < h->snd_n + (size_t)n) cap *= 2;
        /* One allocation for both channels: they must stay the same length, and
         * a partial growth would leave the short one to be overwritten by the
         * next mix. */
        float *grown = realloc(h->snd_l, cap * 2 * sizeof(float));
        if (!grown) return;
        h->snd_l = grown;
        h->snd_r = grown + cap;
        h->snd_cap = cap;
    }
    wb_audio_mix(&h->audio, h->snd_l + h->snd_n, h->snd_r + h->snd_n, n);
    h->snd_n += (size_t)n;
}

int wb_host_step(wb_host *h, float dt) {
    if (!h || !h->L) return -1;
    /* One profiler frame per caller frame, whatever it buys. The frame zone is
     * the budget gate's subject: the whole step must fit the frame it was given,
     * not just the parts of it we thought to time. */
    wb_prof *pr = h->prof;
    wb_prof_begin_frame(pr);
    wb_prof_zone_begin(pr, "frame");
    /* Consume whole fixed steps from the frame time; the remainder is the render
     * alpha, never sim state. The headless host feeds a fixed 1/60, which buys
     * exactly one step per frame -- the same contract a lockstep client keeps. */
    int steps = wb_det_fixed_advance(&h->acc, (double)dt);
    int rc = 0;
    for (int i = 0; i < steps; i++) {
        const uint32_t tick = h->ticks;     /* this step's number, 0-based */
        /* Replay: the recorded actions for this step replace the live input. A
         * log that ends early, or whose ticks do not line up with the run, is
         * itself a divergence -- the run did not reproduce. */
        if (h->replay_log) {
            if (tick >= h->replay_log->n ||
                h->replay_log->frames[tick].tick != tick ||
                h->replay_log->frames[tick].len != sizeof(wb_replay_step)) {
                if (!h->diverged) { h->diverged = 1; h->div_tick = tick; }
                break;
            }
            memcpy(&h->replay_actions, h->replay_log->frames[tick].data,
                   sizeof h->replay_actions);
            h->replaying = 1;
        }
        WB_PROF_ZONE(pr, "script") {
            rc = wb_script_step(h->L, (float)WB_HOST_FIXED_DT);
        }
        if (rc < 0) break;
        /* Input drives the player only when some action is held: with none, the
         * controller must leave the velocity alone, or it would silently brake
         * whatever the mode's own script asked for. */
        wb_actions a;
        WB_PROF_ZONE(pr, "control") {
            wb_host_actions(h, &a);
            if (wb_actions_any(&a))
                wb_character_apply_body(&h->ch, &h->body, &a, &h->cam,
                                        (float)WB_HOST_FIXED_DT);
        }
        /* Remember where every body is BEFORE the step moves it: the renderer
         * lerps from this to the post-step state by the accumulator's remainder
         * (LP-04). Snapshotting here rather than after the tick is what makes the
         * two ends of the lerp exactly one step apart. */
        wb_host_snapshot_prev(h);
        WB_PROF_ZONE(pr, "physics") {
            wb_physics_tick(h->world, (float)WB_HOST_FIXED_DT);
        }
        /* The walk cycle is simulation state, so it advances with the fixed step
         * and never with the frame -- a render must not be able to move it. */
        wb_host_advance_walks(h, WB_HOST_FIXED_DT);
        wb_host_emit_sounds(h);
        h->ticks++;
        h->digest = wb_snap_digest_world(h->world, wb_host_state, 2);
        /* Record what this step did and -- on a replay -- check that the same
         * inputs produced the same state. The digest is the state's checksum, so a
         * mismatch here is the first divergent tick: the exact thing LP-03 can
         * detect and LP-87 exists to LOCALIZE. */
        if (h->rec_log) {
            wb_replay_step s;
            s.fwd = a.fwd; s.back = a.back; s.left = a.left;
            s.right = a.right; s.jump = a.jump; s.sprint = a.sprint;
            s.digest = h->digest;
            wb_replay_record(h->rec_log, tick, &s, sizeof s);
        } else if (h->replay_log && tick < h->replay_log->n &&
                   h->replay_log->frames[tick].len == sizeof(wb_replay_step)) {
            wb_replay_step s;
            memcpy(&s, h->replay_log->frames[tick].data, sizeof s);
            if (s.digest != h->digest && !h->diverged) {
                h->diverged = 1;
                h->div_tick = tick;
            }
        }
        /* The objective runs on the simulation clock, not the frame clock: it
         * advances once per whole step by exactly one fixed step's worth, so a
         * match replays identically however the caller paces its frames. */
        if (h->has_objective)
            wb_objective_step(&h->objective, WB_HOST_FIXED_DT);
    }
    wb_prof_zone_end(pr, "frame");
    h->alpha = wb_interp_alpha(wb_det_fixed_remainder(&h->acc), WB_HOST_FIXED_DT);
    wb_prof_end_frame(pr);
    /* The audio follows the SIMULATED time, not the frame time: whatever whole
     * steps this frame bought, that much sound exists. */
    if (steps > 0) wb_host_mix_audio(h, (double)steps * WB_HOST_FIXED_DT);
    /* The camera follows the INTERPOLATED player position, not the stepped one:
     * a camera that steps while the bodies glide makes the glide invisible. Input
     * edges are consumed once per frame. */
    double p[3];
    if (wb_host_player_pos(h, p) == 0) {
        double rp[3];
        if (wb_host_body_render_pos(h, h->player.index, rp) == 0)
            wb_camera_update(&h->cam, rp, 0.5);
        else
            wb_camera_update(&h->cam, p, 0.5);
    }
    /* The camera is the LISTENER: where the player is looking is where every
     * sound is placed from. Set every frame, because the head turns. */
    wb_audio_set_listener(&h->audio,
                          (float)(h->cam.yaw * 180.0 / 3.14159265358979),
                          0.0f, 0.0f);
    wb_input_end_frame(&h->in);
    return rc < 0 ? -1 : (steps > 0 ? 1 : 0);
}

uint32_t wb_host_ticks(const wb_host *h) {
    return h ? h->ticks : 0;
}

uint64_t wb_host_digest(const wb_host *h) {
    return h ? h->digest : 0;
}

float wb_host_render_alpha(const wb_host *h) {
    return h ? h->alpha : 0.0f;
}

wb_prof *wb_host_profiler(wb_host *h) {
    return h ? h->prof : NULL;
}

int wb_host_prof_summary(wb_host *h, const char *name, wb_prof_stat *out) {
    if (!h || !h->prof || !name || !out) return -1;
    /* wb_prof_summary returns 1 when the slot exists, 0 when it does not; this
     * accessor's contract is the opposite, so translate. */
    return wb_prof_summary(h->prof, name, out) ? 0 : -1;
}

unsigned long wb_host_prof_overruns(const wb_host *h) {
    return (h && h->prof) ? wb_prof_total_overruns(h->prof) : 0;
}

/* ---- The match objective (LP-114) ---------------------------------------- */

/* The script's side of the objective. The host owns the state, so these
 * closures capture the host rather than reading a world pointer from the
 * registry the way the entity bindings do. A mode scores a point with
 * wb.objective_score(side, points) and reads the clock back; it never has to
 * know the rule or the target, which is the host's business. */
static int l_obj_score(lua_State *L) {
    wb_host *h = (wb_host *)lua_touserdata(L, lua_upvalueindex(1));
    uint32_t side = (uint32_t)luaL_checkinteger(L, 1);
    uint32_t points = (uint32_t)luaL_checkinteger(L, 2);
    lua_pushinteger(L, wb_host_score(h, side, points));
    return 1;
}
static int l_obj_death(lua_State *L) {
    wb_host *h = (wb_host *)lua_touserdata(L, lua_upvalueindex(1));
    uint32_t side = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, wb_host_death(h, side));
    return 1;
}
static int l_obj_state(lua_State *L) {
    wb_host *h = (wb_host *)lua_touserdata(L, lua_upvalueindex(1));
    lua_pushinteger(L, wb_host_objective_state(h));
    return 1;
}
static int l_obj_score_of(lua_State *L) {
    wb_host *h = (wb_host *)lua_touserdata(L, lua_upvalueindex(1));
    uint32_t side = (uint32_t)luaL_checkinteger(L, 1);
    lua_pushinteger(L, (lua_Integer)wb_host_objective_score_of(h, side));
    return 1;
}
static int l_obj_clock(lua_State *L) {
    wb_host *h = (wb_host *)lua_touserdata(L, lua_upvalueindex(1));
    lua_pushnumber(L, wb_host_objective_clock(h));
    return 1;
}
static int l_obj_winner(lua_State *L) {
    wb_host *h = (wb_host *)lua_touserdata(L, lua_upvalueindex(1));
    lua_pushinteger(L, wb_host_objective_winner(h));
    return 1;
}

/* A mode's own tuning, read from its manifest's config schema. This is what
 * makes the numbers in mode.json load-bearing rather than decorative: the
 * script asks, the host answers, and the schema's default is the fallback. */
static int l_cfg(lua_State *L) {
    wb_host *h = (wb_host *)lua_touserdata(L, lua_upvalueindex(1));
    const char *name = luaL_checkstring(L, 1);
    double dflt = luaL_optnumber(L, 2, 0.0);
    const wb_mode_manifest *mf =
        (h && h->ws) ? wb_worldspace_mode(h->ws, h->mode) : NULL;
    if (!mf) { lua_pushnumber(L, dflt); return 1; }
    wb_mode_config cfg;
    wb_mode_config_defaults(mf, &cfg);
    int is_int = 0;
    for (int i = 0; i < mf->ncfgs; i++)
        if (strcmp(mf->cfgs[i].name, name) == 0) {
            is_int = (mf->cfgs[i].type == WB_MODE_CFG_INT);
            break;
        }
    double v = is_int ? (double)wb_mode_config_get_int(mf, &cfg, name, (int)dflt)
                      : wb_mode_config_get_float(mf, &cfg, name, dflt);
    lua_pushnumber(L, v);
    return 1;
}

void wb_host_install_bindings(lua_State *L, wb_host *h) {
    if (!L || !h) return;
    lua_getglobal(L, "wb");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    static const struct { const char *name; lua_CFunction fn; } kObj[] = {
        { "cfg",               l_cfg        },
        { "objective_score",   l_obj_score   },
        { "objective_death",   l_obj_death   },
        { "objective_state",   l_obj_state   },
        { "objective_score_of",l_obj_score_of},
        { "objective_clock",   l_obj_clock   },
        { "objective_winner",  l_obj_winner  },
    };
    for (size_t i = 0; i < sizeof kObj / sizeof kObj[0]; i++) {
        lua_pushlightuserdata(L, h);
        lua_pushcclosure(L, kObj[i].fn, 1);
        lua_setfield(L, -2, kObj[i].name);
    }
    lua_pop(L, 1);
}

int wb_host_set_objective(wb_host *h, int rule, uint32_t sides,
                          uint32_t target, double limit_s) {
    if (!h) return -1;
    if (wb_objective_init(&h->objective, rule, sides, target, limit_s,
                           h->err, sizeof h->err) != 0)
        return -1;
    h->has_objective = 1;
    h->err[0] = 0;
    return 0;
}

int wb_host_score(wb_host *h, uint32_t side, uint32_t points) {
    if (!h || !h->has_objective) return WB_OBJ_ONGOING;
    return wb_objective_score(&h->objective, side, points);
}

int wb_host_death(wb_host *h, uint32_t side) {
    if (!h || !h->has_objective) return WB_OBJ_ONGOING;
    return wb_objective_death(&h->objective, side);
}

const wb_objective *wb_host_objective(const wb_host *h) {
    return (h && h->has_objective) ? &h->objective : NULL;
}

int wb_host_objective_state(const wb_host *h) {
    return (h && h->has_objective) ? wb_objective_state(&h->objective)
                                   : WB_OBJ_ONGOING;
}

int wb_host_objective_winner(const wb_host *h) {
    return (h && h->has_objective) ? wb_objective_winner(&h->objective) : -1;
}

uint32_t wb_host_objective_score_of(const wb_host *h, uint32_t side) {
    return (h && h->has_objective) ? wb_objective_score_of(&h->objective, side) : 0;
}

double wb_host_objective_clock(const wb_host *h) {
    return (h && h->has_objective) ? wb_objective_clock(&h->objective) : 0.0;
}

const char *wb_host_objective_state_name(const wb_host *h) {
    if (!h || !h->has_objective) return "none";
    switch (wb_objective_state(&h->objective)) {
    case WB_OBJ_WON:     return "won";
    case WB_OBJ_LOST:    return "lost";
    case WB_OBJ_DRAW:    return "draw";
    default:             return "ongoing";
    }
}

/* ------------------------------------------------------- elapsed time */

/* Seconds of SIMULATION time since the host opened. The objective's own clock
 * when there is one, else the tick count -- so a mode without an objective still
 * has a clock to put things on. */
double wb_host_elapsed(const wb_host *h) {
    if (!h) return 0.0;
    if (h->has_objective) return wb_objective_elapsed(&h->objective);
    return (double)h->ticks * WB_HOST_FIXED_DT;
}

/* ---------------------------------------------------------- onboarding */

const char *wb_host_hint(const wb_host *h) {
    return (h && h->hint[0]) ? h->hint : NULL;
}

/* 1 while the mode's hint should be on screen: it has text, it asked for a
 * duration, and that duration has not run out. */
int wb_host_hint_visible(const wb_host *h) {
    if (!h || !h->hint[0] || h->hint_seconds <= 0.0) return 0;
    return wb_host_elapsed(h) < h->hint_seconds;
}

/* --------------------------------------------------------------- audio */

/* The match's audio: the accumulated STEREO mix (left and right) and the number
 * of frames in it. The buffers are the host's; the caller writes them out (the
 * engine writes a WAV) and must not free them. */
size_t wb_host_audio(const wb_host *h, const float **left, const float **right) {
    if (!h) {
        if (left) *left = NULL;
        if (right) *right = NULL;
        return 0;
    }
    if (left) *left = h->snd_l;
    if (right) *right = h->snd_r;
    return h->snd_n;
}

uint64_t wb_host_audio_events(const wb_host *h) {
    return h ? wb_audio_emitted(&h->audio) : 0;
}

int wb_host_audio_rate(const wb_host *h) {
    return h ? h->audio.rate : 0;
}

/* ---- Input and the player controller ------------------------------------- */

wb_input *wb_host_input(wb_host *h) { return h ? &h->in : NULL; }

void wb_host_input_key_down(wb_host *h, int code) {
    if (h) wb_input_key_down(&h->in, code);
}
void wb_host_input_key_up(wb_host *h, int code) {
    if (h) wb_input_key_up(&h->in, code);
}
void wb_host_input_button(wb_host *h, int button, int down) {
    if (h) wb_input_button(&h->in, button, down);
}
void wb_host_input_mouse_move(wb_host *h, double dx, double dy) {
    if (h) wb_input_mouse_move(&h->in, dx, dy);
}
void wb_host_input_wheel(wb_host *h, double steps) {
    if (h) wb_input_wheel(&h->in, steps);
}

void wb_host_set_keys(wb_host *h, const wb_host_keys *keys) {
    if (h && keys) h->keys = *keys;
}

void wb_host_actions(const wb_host *h, wb_actions *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!h) return;
    if (h->replaying) { *out = h->replay_actions; return; }
    out->fwd    = wb_input_down(&h->in, h->keys.fwd);
    out->back   = wb_input_down(&h->in, h->keys.back);
    out->left   = wb_input_down(&h->in, h->keys.left);
    out->right  = wb_input_down(&h->in, h->keys.right);
    out->jump   = wb_input_pressed(&h->in, h->keys.jump);   /* edge, not level */
    out->sprint = wb_input_down(&h->in, h->keys.sprint);
}

wb_character *wb_host_character(wb_host *h) { return h ? &h->ch : NULL; }
wb_camera *wb_host_camera(wb_host *h) { return h ? &h->cam : NULL; }

int wb_host_player_pos(const wb_host *h, double p[3]) {
    if (!h || !p) return -1;
    wb_ecs_body_ctx c = { h->world, h->player.index };
    float *f = wb_ecs_comp(&c, WB_COMP_POSITION);
    if (!f) return -1;
    p[0] = f[0];
    p[1] = f[1];
    p[2] = f[2];
    return 0;
}

/* Where a body is DRAWN, as opposed to where it is SIMULATED: the previous
 * step's position lerped toward the current one by the fixed accumulator's
 * remainder (LP-04). `out` is the world position, z-up. Returns 0 on success --
 * including for a body with no previous state, which is reported at its
 * simulated position -- and -1 when the body has no position at all. */
int wb_host_body_render_pos(const wb_host *h, uint32_t index, double out[3]) {
    if (!h || !h->world || !out) return -1;
    size_t esz = wb_world_comp_elem_size(h->world, WB_COMP_POSITION);
    uint8_t *base = (uint8_t *)wb_world_comp_data(h->world, WB_COMP_POSITION);
    if (!base || !wb_world_slot_alive(h->world, index) ||
        !wb_world_comp_present(h->world, WB_COMP_POSITION, index))
        return -1;
    const float *cur = (const float *)(base + (size_t)index * esz);
    if (index < h->prev_n && h->prev_ok && h->prev_ok[index] && h->prev_pos) {
        const float *prev = &h->prev_pos[(size_t)index * 3];
        float w[3];
        wb_interp_lerp3(prev, cur, h->alpha, w);
        out[0] = w[0]; out[1] = w[1]; out[2] = w[2];
        return 0;
    }
    out[0] = cur[0]; out[1] = cur[1]; out[2] = cur[2];
    return 0;
}

/* ---- Replay (LP-87) -------------------------------------------------------- */

int wb_host_record_start(wb_host *h, wb_replay *r) {
    if (!h || !r || !r->frames) return -1;
    h->rec_log = r;
    h->replay_log = NULL;
    h->replaying = 0;
    h->diverged = 0;
    h->div_tick = 0;
    return 0;
}

int wb_host_replay_start(wb_host *h, const wb_replay *r) {
    if (!h || !r || !r->frames || r->n == 0) return -1;
    h->replay_log = r;
    h->rec_log = NULL;
    h->replaying = 0;
    h->diverged = 0;
    h->div_tick = 0;
    return 0;
}

int wb_host_replay_diverged(const wb_host *h) {
    return h ? h->diverged : 0;
}

uint32_t wb_host_replay_divergence_tick(const wb_host *h) {
    return h ? h->div_tick : 0;
}

int wb_host_render(wb_host *h, wb_framebuffer *fb) {
    if (!h || !fb || !fb->pixels || fb->width <= 0 || fb->height <= 0) return -1;

    wb_render_clear(fb, WB_RENDER_SKY);

    /* The follow camera is z-up, the renderer is y-up: map (x, y, z) to
     * (x, z, y). The camera's declared target is the point we look at -- the
     * eye is derived from it by the orbit, not the other way round. */
    double eye[3];
    wb_camera_eye(&h->cam, eye);
    const float re[3] = { (float)eye[0], (float)eye[2], (float)eye[1] };
    const float rt[3] = { (float)h->cam.target[0],
                          (float)h->cam.target[2],
                          (float)h->cam.target[1] };

    wb_render_camera rc;
    rc.eye[0] = re[0];            rc.eye[1] = re[1];            rc.eye[2] = re[2];
    rc.at[0]  = rt[0];            rc.at[1]  = rt[1];            rc.at[2]  = rt[2];
    rc.up[0]  = 0.0f;              rc.up[1]  = 1.0f;              rc.up[2]  = 0.0f;
    rc.vfov_deg = 60.0f;
    rc.near_z   = 0.1f;
    rc.far_z    = 1000.0f;

    int drawn = 0;
    /* The figure is built in the camera's own frame, so the stride reads from
     * whatever angle the camera sits at. The camera's axes are world z-up and the
     * renderer is y-up, so they are mapped the same way as the body positions. */
    double right_w[3], forward_w[3];
    wb_camera_right(&h->cam, right_w);
    wb_camera_forward(&h->cam, forward_w);
    const float right_f[3]  = { (float)right_w[0], (float)right_w[2],
                                (float)right_w[1] };
    const float forward_f[3] = { (float)forward_w[0], (float)forward_w[2],
                                 (float)forward_w[1] };
    uint32_t n = wb_world_next_index(h->world);
    for (uint32_t i = 0; i < n; i++) {
        if (!wb_world_slot_alive(h->world, i)) continue;
        if (!wb_world_comp_present(h->world, WB_COMP_POSITION, i)) continue;
        /* Where the body is DRAWN, not where it is simulated: the previous step
         * lerped toward this one by the accumulator's remainder (LP-04). */
        double rp[3];
        if (wb_host_body_render_pos(h, i, rp) != 0) continue;
        const float w[3] = { (float)rp[0], (float)rp[2], (float)rp[1] };
        int is_player = (i == h->player.index);
        uint32_t col = is_player ? 0xFFE080 : 0xC0C8D0;
        /* A body with no walk state (the table could not grow) falls back to the
         * plain marker rather than drawing nothing. */
        if (h->walks && i < h->nwalks) {
            wb_host_draw_figure(fb, &rc, w, right_f, forward_f,
                                &h->walks[i], col);
            drawn++;
        } else {
            drawn += wb_render_marker(fb, &rc, w,
                                      is_player ? 0.5f : 0.35f, col);
        }
    }

    /* The HUD is the last layer: it is screen-space, not world-space, so it is
     * drawn after every marker and never occluded by one. This is the stone that
     * makes the objective visible -- the score and the clock exist in the host
     * and now they are on the screen. */
    if (h->has_objective)
        wb_hud_match(fb, 12, 12, wb_host_objective_state_name(h),
                     wb_host_objective_score_of(h, 0),
                     wb_host_objective_score_of(h, 1),
                     wb_host_objective_clock(h), 0xFFF2C0, 2);

    /* Onboarding goes on last of all: a first-time player is told what to do,
     * for as long as the mode asked. */
    if (wb_host_hint_visible(h))
        wb_hud_hint(fb, h->hint, 0xFFF2C0, 1);

    return drawn;
}

int wb_host_running(wb_host *h) {
    if (!h || !h->L) return 0;
    lua_getglobal(h->L, "wb");
    if (!lua_istable(h->L, -1)) { lua_pop(h->L, 1); return 0; }
    lua_getfield(h->L, -1, "running");
    int running = lua_isnil(h->L, -1) ? 1 : lua_toboolean(h->L, -1);
    lua_pop(h->L, 2);
    /* A finished objective ends the match. The mode's script does not have to
     * know the score or the clock -- that is the point of having a match clock:
     * the host stops when the match is over, not when a tick counter says so. */
    if (h->has_objective && wb_objective_is_final(&h->objective)) return 0;
    return running;
}
