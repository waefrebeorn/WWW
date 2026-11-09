/*
 * wb_worldspace.c -- the spawn-a-worldspace facade. See wb_worldspace.h.
 */
#include "wb_worldspace.h"
#include "wb_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wb_worldspace {
    wb_dist_registry *reg;
    wb_mode_manifest *modes;
    wb_spawn_set    **spawns;
    char            **dirs;
    int nmodes, cap;
};

wb_worldspace *wb_worldspace_open(void) {
    wb_worldspace *ws = (wb_worldspace *)calloc(1, sizeof *ws);
    if (!ws) return NULL;
    ws->reg = wb_dist_registry_new();
    if (!ws->reg) { free(ws); return NULL; }
    return ws;
}

void wb_worldspace_close(wb_worldspace *ws) {
    if (!ws) return;
    for (int i = 0; i < ws->nmodes; i++) {
        wb_spawn_free(ws->spawns[i]);
        free(ws->dirs[i]);
    }
    free(ws->modes);
    free(ws->spawns);
    free(ws->dirs);
    wb_dist_registry_free(ws->reg);
    free(ws);
}

static const wb_mode_manifest *find_mode(const wb_worldspace *ws, const char *name) {
    if (!ws || !name) return NULL;
    for (int i = 0; i < ws->nmodes; i++)
        if (!strcmp(ws->modes[i].name, name)) return &ws->modes[i];
    return NULL;
}

int wb_worldspace_register_mode(wb_worldspace *ws, const char *dir,
                                char *err, size_t err_sz) {
    if (!ws || !dir) {
        if (err) snprintf(err, err_sz, "null worldspace or dir");
        return -1;
    }
    char path[600];
    wb_join_path(path, sizeof path, dir, "mode.json");

    if (ws->nmodes >= ws->cap) {
        int nc = ws->cap ? ws->cap * 2 : 8;
        wb_mode_manifest *nm = (wb_mode_manifest *)realloc(ws->modes, (size_t)nc * sizeof *nm);
        if (!nm) { if (err) snprintf(err, err_sz, "out of memory"); return -1; }
        ws->modes = nm;
        ws->spawns = (wb_spawn_set **)realloc(ws->spawns, (size_t)nc * sizeof *ws->spawns);
        ws->dirs = (char **)realloc(ws->dirs, (size_t)nc * sizeof *ws->dirs);
        if (!ws->spawns || !ws->dirs) { if (err) snprintf(err, err_sz, "out of memory"); return -1; }
        ws->cap = nc;
    }

    wb_mode_manifest *m = &ws->modes[ws->nmodes];
    if (wb_mode_manifest_load(path, m) != 0) {
        if (err) snprintf(err, err_sz, "%s", m->error);
        return -1;
    }
    if (wb_mode_manifest_validate(m, err, err_sz) != 0) return -1;
    if (find_mode(ws, m->name)) {
        if (err) snprintf(err, err_sz, "gametype '%s' is already registered", m->name);
        return -1;
    }

    ws->spawns[ws->nmodes] = wb_spawn_new();
    if (!ws->spawns[ws->nmodes]) { if (err) snprintf(err, err_sz, "out of memory"); return -1; }
    ws->dirs[ws->nmodes] = strdup(dir);
    if (!ws->dirs[ws->nmodes]) { if (err) snprintf(err, err_sz, "out of memory"); return -1; }

    ws->nmodes++;
    return 0;
}

int wb_worldspace_mode_count(const wb_worldspace *ws) { return ws ? ws->nmodes : 0; }

const wb_mode_manifest *wb_worldspace_mode(const wb_worldspace *ws, const char *name) {
    return find_mode(ws, name);
}

int wb_worldspace_mode_dir(const wb_worldspace *ws, const char *name,
                           char *out, size_t out_sz) {
    if (!ws || !name || !out || out_sz == 0) return -1;
    for (int i = 0; i < ws->nmodes; i++) {
        if (!strcmp(ws->modes[i].name, name)) {
            snprintf(out, out_sz, "%s", ws->dirs[i]);
            return 0;
        }
    }
    return -1;
}

int wb_worldspace_add_spawn(wb_worldspace *ws, const char *gamemode,
                            const float pos[3], int team, const char *tag) {
    if (!ws || !gamemode || !pos) return -1;
    for (int i = 0; i < ws->nmodes; i++) {
        if (!strcmp(ws->modes[i].name, gamemode))
            return wb_spawn_add(ws->spawns[i], pos, team, tag, gamemode);
    }
    return -1;
}

int wb_worldspace_spawn_count(const wb_worldspace *ws, const char *gamemode) {
    if (!ws) return 0;
    for (int i = 0; i < ws->nmodes; i++)
        if (!strcmp(ws->modes[i].name, gamemode))
            return wb_spawn_count(ws->spawns[i]);
    return 0;
}

wb_dist_registry *wb_worldspace_registry(wb_worldspace *ws) {
    return ws ? ws->reg : NULL;
}

int wb_worldspace_spawn(wb_worldspace *ws, const char *gamemode,
                        const wb_mode_config *cfg, const char *player_id,
                        int team, char *out_instance, float out_pos[3],
                        char *err, size_t err_sz) {
    if (!ws || !gamemode) {
        if (err) snprintf(err, err_sz, "null worldspace or gamemode");
        return -1;
    }
    const wb_mode_manifest *m = find_mode(ws, gamemode);
    if (!m) {
        if (err) snprintf(err, err_sz, "unknown gametype '%s'", gamemode);
        return -1;
    }

    /* NULL config == every schema default. */
    wb_mode_config defaults;
    const wb_mode_config *use = cfg;
    if (!use) {
        wb_mode_config_defaults(m, &defaults);
        use = &defaults;
    }
    if (wb_mode_config_validate(m, use, err, err_sz) != 0) return -1;

    /* Pick the spawn point FIRST. A mode with no free spawn must fail without
     * leaving an empty instance behind. */
    int si = -1, idx = -1;
    for (int i = 0; i < ws->nmodes; i++) {
        if (strcmp(ws->modes[i].name, gamemode)) continue;
        si = i;
        idx = wb_spawn_pick(ws->spawns[i], gamemode, team, out_pos);
        break;
    }
    if (si < 0) {
        if (err) snprintf(err, err_sz, "gametype '%s' is not registered", gamemode);
        return -1;
    }
    if (idx < 0) {
        if (err) snprintf(err, err_sz,
                          "gametype '%s' has no spawn point for team %d",
                          gamemode, team);
        return -1;
    }

    /* Matchmake, or open a fresh instance sized by the mode's config. */
    wb_dist_instance *in = wb_dist_join_best(ws->reg, gamemode, player_id, out_instance);
    if (!in) {
        int cap = wb_mode_config_get_int(m, use, "max_players", m->max_players);
        if (cap < 1) cap = m->max_players;
        in = wb_dist_instance_new(gamemode, cap, "www");
        if (!in) { if (err) snprintf(err, err_sz, "cannot create an instance"); return -1; }
        wb_dist_registry_add(ws->reg, in);
        wb_dist_instance_join(in, player_id);
        snprintf(out_instance, WB_DIST_ID_LEN, "%s", wb_dist_instance_id(in));
    }

    wb_spawn_occupy(ws->spawns[si], idx);
    return 0;
}
