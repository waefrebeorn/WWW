# Slices 9–18 — kernel → meta engine

Status tracker for the roadmap in `docs/meta-engine-assessment.md`.
Each slice ships real, tested code; nothing is marked done without a green gate.

| Slice | Pillar | Deliverable | Gate | Status |
|---|---|---|---|---|
| 9 | physics | MuJoCo 4 backend: bodies, collision, character controller, ECS binding | `make test-physics` — 27 | ✅ `7a56adf` |
| 10 | input + camera | input state, follow camera, input→character chain, headless playable demo | `make test-character` | ✅ |
| 11 | GPU backend | render graph + GPU backend behind the software rasterizer's geometry API | `make test-gpu` — 26 | ✅ |
| 12 | content pipeline | glTF/GLB import → GPU-ready, content-addressed DDC, manifest | `make test-assets` — 30 | ✅ |
| 13 | networking | authoritative server, client prediction, AOI on the quadkey | `make test-net` — 28 | ✅ |
| 14 | avatar + identity | glTF+VRM humanoid import, rig, cross-world identity | `make test-avatar` | ⬜ |
| 15 | modding sandbox | untrusted-code sandbox (Luau model), mod manifest, hot reload | `make test-mod` | ⬜ |
| 16 | experience distribution | portals/stadiums, instance matchmaking, discovery hooks | `make test-exp2` | ⬜ |
| 17 | editor | in-engine world editing + live edit | `make test-editor` | ⬜ |
| 18 | server meshing | authority handover, per-component authority, persistent entity streaming | `make test-mesh` | ⬜ |

## Running totals

`make test` currently: ECS 21 · SPATIAL 142 · TILES 49 · RENDER 6 · EXP 15 ·
LV 17 · LV-SCRIPT 5 · PHYSICS 27 = **282 assertions, 0 fail**.

## Design rules carried forward

- One spatial index (the quadkey core) for AOI + streaming + sharding. Never fork
  a second index.
- Physics owns transforms for bound entities; the ECS holds the game-logic view.
- C11 owns the core; Lua/slerm-Lua owns game logic. No managed runtime.
- Every slice gets a real test harness with pass/fail counts. No gate, no commit.
