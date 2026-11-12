# wubu-engine — the ~WuBu~ world-space engine

The C11 + Lua engine that builds the world space. It lives **inside the
[WWW](https://github.com/waefrebeorn/WWW) (World Wide WorldSpace) repository**,
at `WWW/~WuBu~` — the ~WuBu~ space kept inside the World Wide WorldSpace.

Lua is the "C# replacement" (game logic, GUI, mods); C11 owns the core:
memory, the ECS, the quadkey spatial index, physics and the GPU backend.
Lua never allocates core memory.

## Build & run

Requires MinGW64 gcc (`mingw32-make`, not `make`).

```bash
mkdir -p build          # do this first -- see the Makefile quirk below
mingw32-make all        # -> build/wubu_engine.exe
mingw32-make test       # 88 gates, 8457 assertions
mingw32-make run        # runs the omniverse-hub gametype
./build/wubu_engine.exe games omniverse-hub
```

`make all` cannot create `build/` itself: make spawns a restricted `mkdir.exe`
that Windows refuses. Create the directory once in the shell and the makefile
skips its own `mkdir` rule.

## What is here

| Path | What |
|------|------|
| `src/core/wb_ecs.*` | sparse-set + SoA ECS, opaque world handle |
| `src/core/wb_spatial.*` | quadkey lon/lat keying (the world's spine) |
| `src/core/wb_tiles.*` | quadkey tile store + LRU streaming |
| `src/core/wb_render.*` | software 3D rasterizer (the portable floor) |
| `src/core/wb_exp.*` | experience containers — one Lua state + ECS each |
| `src/core/wb_physics_*` | MuJoCo: world / body / ECS binding |
| `src/core/wb_gpu*` | D3D12 RHI + render graph: dev/buffer/texture/shader/pass/exec |
| `src/core/wb_assets_*` | content pipeline: glTF parse / DDC / manifest |
| `src/core/wb_avatar_*` | avatar: bones / glTF load / anim sampling / identity |
| `src/core/wb_edit.*` | in-world editor (ops, undo/redo, JSON replay) |
| `src/core/wb_dist.*` | experience distribution (instances, matchmaking, portals) |
| `src/core/wb_mode.*` | gametype manifests + typed config schema + declared spawns (slice 19) |
| `src/core/wb_spawn.*` | spawn points: team / gametype tagged, reservable |
| `src/core/wb_worldspace.*` | the spawn-a-worldspace facade (slice 19) + stadium gates (slice 20) |
| `src/core/wb_host.*` | the gametype host: scan a directory of modes, run one (slice 22) |
| `src/lua/wb_space_bind.*` | the gamemode-facing space: `wb.add_spawn` / `add_gate` / `gate_at` / `travel` / `instances` (slices 21–22) |
| `src/core/wb_util.*` | shared helpers: file IO, FNV-1a hash, error buffer, paths |
| `src/core/wb_gltf.*` | shared glTF accessor decoding + GLB splitting |
| `src/lua/` | Lua 5.4 host + `wb.*` bindings + the modding sandbox |
| `src/lv/` | slerm-Lua: the C11 interpreter (arena/value/lex/parse/eval/host) |
| `src/core/wb_hive.*`, `wb_tagheap.*`, `wb_budget.*`, `wb_frame.*`, `wb_tls.*`, `wb_atomic.*`, `wb_pool.*` | the hive memory substrate (see below) |
| `src/net/` | networking: AOI / server / client / loopback + server meshing |
| `tests/` | one harness per module |
| `games/` | Lua game scripts |
| `docs/` | design notes + the meta-engine assessment |

### The hive modules come from WuBuHIVE

`wb_hive`, `wb_tagheap`, `wb_budget`, `wb_frame`, `wb_tls`, `wb_atomic` and
`wb_pool` are kept in sync with
[WuBuHIVE](https://github.com/waefrebeorn/WuBuHIVE), where this memory substrate
is extracted for reuse across all wubu projects. **Fix them there, then sync
them back** — the copies here carry the same `wb_` prefix so the sync is a
byte-for-byte copy, and a divergence shows up as a diff.

## Status

All 22 roadmap slices are done — ECS, spatial, tiles, render, experience
containers, slerm-Lua, physics, input/camera/character, GPU, content pipeline,
networking, avatar/identity, modding sandbox, distribution, editor, server
meshing, gametypes, stadium travel, the gamemode-facing space, and the
gametype host. `docs/meta-engine-assessment.md` is the strategic read: the five
platform pillars and the slice roadmap. `../docs/WWW.md` is the doctrine of the
space: what it is for, and what it refuses to be.

`docs/EXPERIENCE-ASSESSMENT.md` is the companion read. It measures how much of
the engine the shipped binary actually reaches — **11 of 92 modules** — separates
the player experience from the creator experience, and proposes the validation
mechanisms (vertical-slice gate, bot playtests, experience telemetry, the
creator-workflow gate) that module gates cannot provide. Run
`python3 tools/wiring_report.py` to reproduce the wiring and reachability numbers.

A gametype is a declared, parameterisable package (`mode.json` + an entry
script + a typed config schema). The engine knows modes, configs, spawns and
instances — it does not know games. That ignorance is what lets all gametypes
work.

Point the engine at a directory of gametypes and name one:

```bash
./build/wubu_engine.exe games omniverse-hub [player-id]
```

Every subdirectory holding a `mode.json` is registered (so a mode's gates can
name any sibling), the named mode's entry script runs with its space bindings,
the player is materialized in that mode's world at a spawn point the mode
declared, and the world steps until the mode sets `wb.running = false`. The
`games/` tree ships two modes — `omniverse-hub`, a hub with a rift into
`duel-stadium`, and the stadium itself.

A mode's own script declares its stadiums and travels players between worlds:

```lua
wb.add_spawn(0, 0, 0, -1, "arrival")                      -- runtime spawn
local gate = wb.add_gate("duel-stadium", "landing", 10, 0, 0, 2)

function step(dt)
  local g = wb.gate_at(10, 0.5, 0)                        -- the walk-in
  if g then
    local ok, inst, x, y, z = wb.travel(g, wb.player_id, -1)
  end
  local worlds = wb.instances()                           -- the live directory
end
```

`wb.add_gate(dst, tag, x, y, z, radius)` declares a gate in the mode's own
world; `wb.travel(gate, player, team, cfg)` carries the player into the
destination gametype's instance at its landing tag with the same identity (the
empty source world is reaped). A mode only ever sees its own world.

Spawn points are **declared in `mode.json`**, not created by the script, so
they exist before any script runs — that is what lets one mode travel into
another. `wb.add_spawn` is for a mode that needs to add points at runtime.

## Module rules

- **Opaque structs.** Module state is a handle from a `_new`/`_create` call;
  the struct lives in a private `*_internal.h` included only by that module's
  own `.c` files.
- **Minimal includes.** One concern per header.
- **C11 only.** The D3D12 backend is compiled as C++ behind `extern "C"`
  because MinGW's `d3d12.h` needs it; the API stays C11.
- **No duplicated helpers.** Shared code lives in `wb_util` and `wb_gltf`.
- **The Lua bridge stays generic.** `wb_script` carries an opaque space context
  but never references the worldspace stack; the `wb.*` space bindings live in
  `wb_space_bind`, which a host links only when it wants them.
