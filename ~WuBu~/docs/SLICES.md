# Slices 9–26 — kernel → meta engine → the gametype host → audio → interpolation → replay → fuzzing

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
| 23 | audio | a synthesiser and a mixer: Big Mac's oscillator, ADSR, biquad and FDN reverb, an HRTF binaural panner, a stereo positional mix, and every walking body's footfall placed in the world | `make test-audio` — 47 · `make test-synth` — 34 · `make test-wav` — 22 | ✅ |
| 24 | render interpolation | the frame is drawn BETWEEN sim steps: the previous step's positions lerped by the fixed accumulator's remainder, and the camera follows the same interpolated point | `make test-host` — 56 | ✅ |
| 25 | record / replay | a run is logged as the actions and digest of every fixed step, re-run from the log alone, and compared: the first divergent tick is the difference between "the checksum disagreed" and "here is where" | `make test-host` — 73 · `make test-replay` | ✅ |
| 26 | headless fuzzing | the input is driven by a seeded pseudo-random action stream; the same seed always produces the same stream, so a failing case is replayable -- and each run is re-played from its log and compared | `make test-host` — 82 · the slice | ✅ |

## Running totals

`make test` currently: **96 gates, 8800 assertions, 0 fail.**

ECS 21 · SPATIAL 142 · TILES 49 · RENDER 12 · EXP 21 · LV 58 · LV-SCRIPT 14 ·
PHYSICS 27 · CHARACTER 29 · GPU 26 · ASSET 30 · NET 28 · AVATAR 32 · MOD 21 ·
DIST 41 · EDIT 37 · MESH 37 · MODE 48 · SPAWN 37 · WORLDSPACE 53 · PORTAL 22 ·
SPACE 27 · HOST 82 · WALK 21 · AUDIO 47 · SYNTH 34 · WAV 22 · DSP 24 · OBJECTIVE 48 ·
HUD 43 · RENDER 20 · WINDOW 16 ·
PROF 29 · DET 15 · SNAP 17 · MATH 22 · FASTMATH 7 · INTERP 14 · JOB 12 ·
POOL 5074 · ACCESS 26 · ATOMIC 8 · FRAME 21 · HIVE 46 · TAGHEAP 21 · THREADHEAP 14 · BUDGET 23 ·
GEOM 10 · MESHLET 12 · LOD 22 · HLOD 14 · GALLOC 27 · OCCL 21 · BINDLESS 23 ·
STREAM 15 · MATERIAL 10 · COMPACT 12 · TAA 11 · DELTA 22 · SNAPSHOT 23 ·
HISTORY 23 · ROLLBACK 19 · NETMODEL 17 · HANDOFF 33 · ASSETGRAPH 21 · MATCH 13 ·
SHADERVAR 28 · WATCH 25 · CVAR 49 · I18N 31 · REPLAY 50 · VALIDATE 31 ·
COOK 35 · PATCH 33 · TLS 16 · EVENTLOG 59 · SOCIAL 57 · CATALOG 48 ·
MODERATE 39 · AGEGATE 45 · LEDGER 76 · FED 72 · RESV 88 · QKEY 95 · ADDR 77 ·
PLACE 57 · LAYER 63 · INTEREST 125 · TRAVEL 83 · LODCHAIN 67 · PERSIST 93 ·
PLANETS 96 · QUOTA 144 · PROV 74 · GEOLIC 105 · PLACEVAL 30 · HEIGHTFIELD 55.

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
