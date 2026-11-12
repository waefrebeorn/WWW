/*
 * wb_host.c -- the gametype host (slice 22). See wb_host.h.
 */
#include "wb_host.h"
#include "wb_character.h"
#include "wb_det.h"
#include "wb_input.h"
#include "wb_interp.h"
#include "wb_prof.h"
#include "wb_script.h"
#include "wb_snap.h"
#include "wb_space_bind.h"
#include "wb_worldspace.h"
#include "wb_util.h"

#include <lua.h>
#include <lauxlib.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    h->err[0] = 0;
    return h;

fail:
    if (err) snprintf(err, err_sz, "%s", h->err);
    wb_host_close(h);
    return NULL;
}

void wb_host_close(wb_host *h) {
    if (!h) return;
    if (h->L) lua_close(h->L);
    if (h->prof) wb_prof_dispose(h->prof);
    if (h->world) wb_world_destroy(h->world);
    if (h->ws) wb_worldspace_close(h->ws);
    free(h);
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
        WB_PROF_ZONE(pr, "script") {
            rc = wb_script_step(h->L, (float)WB_HOST_FIXED_DT);
        }
        if (rc < 0) break;
        /* Input drives the player only when some action is held: with none, the
         * controller must leave the velocity alone, or it would silently brake
         * whatever the mode's own script asked for. */
        WB_PROF_ZONE(pr, "control") {
            wb_actions a;
            wb_host_actions(h, &a);
            if (wb_actions_any(&a))
                wb_character_apply_body(&h->ch, &h->body, &a, &h->cam,
                                        (float)WB_HOST_FIXED_DT);
        }
        WB_PROF_ZONE(pr, "physics") {
            wb_physics_tick(h->world, (float)WB_HOST_FIXED_DT);
        }
        h->ticks++;
        h->digest = wb_snap_digest_world(h->world, wb_host_state, 2);
    }
    wb_prof_zone_end(pr, "frame");
    h->alpha = wb_interp_alpha(wb_det_fixed_remainder(&h->acc), WB_HOST_FIXED_DT);
    wb_prof_end_frame(pr);
    /* The camera follows the player; input edges are consumed once per frame. */
    double p[3];
    if (wb_host_player_pos(h, p) == 0) wb_camera_update(&h->cam, p, 0.5);
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
    size_t esz = wb_world_comp_elem_size(h->world, WB_COMP_POSITION);
    uint8_t *base = (uint8_t *)wb_world_comp_data(h->world, WB_COMP_POSITION);
    uint32_t n = wb_world_next_index(h->world);
    for (uint32_t i = 0; i < n; i++) {
        if (!wb_world_slot_alive(h->world, i)) continue;
        if (!wb_world_comp_present(h->world, WB_COMP_POSITION, i)) continue;
        float *p = (float *)(base + (size_t)i * esz);
        const float w[3] = { p[0], p[2], p[1] };       /* z-up -> y-up */
        int is_player = (i == h->player.index);
        drawn += wb_render_marker(fb, &rc, w,
                                  is_player ? 0.5f : 0.35f,
                                  is_player ? 0xFFE080 : 0xC0C8D0);
    }
    return drawn;
}

int wb_host_running(wb_host *h) {
    if (!h || !h->L) return 0;
    lua_getglobal(h->L, "wb");
    if (!lua_istable(h->L, -1)) { lua_pop(h->L, 1); return 0; }
    lua_getfield(h->L, -1, "running");
    int running = lua_isnil(h->L, -1) ? 1 : lua_toboolean(h->L, -1);
    lua_pop(h->L, 2);
    return running;
}
