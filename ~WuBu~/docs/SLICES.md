# Slices 9–22 — kernel → meta engine → the gametype host

Status tracker for the roadmap in `docs/meta-engine-assessment.md`.
The hop-by-hop backlog — 113 pads in dependency order, each with its sources —
is `docs/LILYPADS.md`. The same backlog turned into an ordered, status-tracked
execution list is `docs/WORKLIST.md`.
Each slice ships real, tested code; nothing is marked done without a green gate.

| Slice | Pillar | Deliverable | Gate | Status |
|---|---|---|---|---|
| 9 | physics | MuJoCo 4 backend: bodies, collision, character controller, ECS binding | `make test-physics` — 27 | ✅ `7a56adf` |
| 10 | input + camera | input state, follow camera, input→character chain, headless playable demo | `make test-character` | ✅ |
| 11 | GPU backend | render graph + GPU backend behind the software rasterizer's geometry API | `make test-gpu` — 26 | ✅ |
| 12 | content pipeline | glTF/GLB import → GPU-ready, content-addressed DDC, manifest | `make test-assets` — 30 | ✅ |
| 13 | networking | authoritative server, client prediction, AOI on the quadkey | `make test-net` — 28 | ✅ |
| 14 | avatar + identity | glTF+VRM humanoid import, rig, cross-world identity | `make test-avatar` — 32 | ✅ |
| 15 | modding sandbox | untrusted-code sandbox (Luau model), mod manifest, hot reload | `make test-mod` — 21 | ✅ |
| 16 | experience distribution | portals/stadiums, instance matchmaking, discovery hooks | `make test-dist` — 41 | ✅ |
| 17 | editor | in-engine world editing + live edit | `make test-edit` — 37 | ✅ |
| 18 | server meshing | authority handover, per-component authority, persistent entity streaming | `make test-mesh` — 37 | ✅ |
| 19 | gametypes | mode manifests + typed config schema, spawn points, the spawn-a-worldspace facade | `make test-mode` — 42 · `make test-spawn` — 37 · `make test-worldspace` — 53 | ✅ |
| 20 | stadium travel | Yu-Gi-Oh portals: walk into a gate, leave every world, spawn into another gametype's instance at a tagged landing, same identity, empty source reaped | `make test-portal` — 22 | ✅ |
| 21 | gamemode-facing space | the stadium layer exposed to scripts: `wb.add_gate` / `gate_at` / `travel` / `instances` from inside the sandbox, attributed to the mode's own world | `make test-space` — 25 | ✅ |
| 22 | gametype host | the engine binary runs a gametype package: scan a directory of modes, register each with its declared spawns, run the mode's script, materialize the player, step the world | `make test-host` — 20 | ✅ |

## Running totals

`make test` currently: **51 gates, 6380 assertions, 0 fail.**

ECS 21 · SPATIAL 142 · TILES 49 · RENDER 6 · EXP 15 · LV 58 · LV-SCRIPT 14 ·
PHYSICS 27 · CHARACTER 29 · GPU 26 · ASSETS 30 · NET 28 · AVATAR 32 · MOD 21 ·
DIST 41 · EDIT 37 · MESH 37 · MODE 48 · SPAWN 37 · WORLDSPACE 53 · PORTAL 22 ·
SPACE 27 · HOST 20.

## Design rules carried forward

- One spatial index (the quadkey core) for AOI + streaming + sharding. Never fork
  a second index.
- Physics owns transforms for bound entities; the ECS holds the game-logic view.
- C11 owns the core; Lua/slerm-Lua owns game logic. No managed runtime.
- Every slice gets a real test harness with pass/fail counts. No gate, no commit.
- The Lua bridge (`wb_script`) carries an opaque space context but never
  references the worldspace stack; the bindings live in `wb_space_bind`, so a
  gate that only needs the generic bridge does not link the world.
- **A mode's spawn points are declared data, not script side effects.** They
  live in `mode.json` so they exist before any script runs — which is what makes
  one mode able to travel INTO another. (`wb.add_spawn` exists for a mode that
  needs to add points at runtime; it cannot be the only source.)
