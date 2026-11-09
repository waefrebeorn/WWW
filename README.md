# wubu-engine

The C11 game engine that ships with WuBuOS — the metaverse flagship.
Lua is the "C# replacement" (game logic + GUI scripting); C11 owns the core
(memory, ECS, physics integration, world ownership).

This repo is the first real slice: a data-oriented ECS + embedded Lua 5.4 host,
proving the scripted-C11-simulation contract end to end.

## State (this commit)

- **ECS core** (`src/core/wb_ecs.c/h`) — sparse-set + SoA. Entities are
  generation-tagged handles into a dense pool; components live in per-type
  contiguous columns. O(1) lookup, cache-friendly iteration, dynamic comp sets.
  Replaces the wrong-shaped AoE union ECS found in `WuBuOS/src/worldsim/`.
- **Lua host** (`src/lua/wb_lua_host.c`) — embeds stock Lua 5.4 (vendored at
  `thirdparty/lua5.4/`). Bindings: `wb.spawn/set_pos/set_vel/set_mass/get_pos/count`.
  Lua drives entity state; C11 integrates physics each frame.
- **Verified**: `games/main.lua` runs 180 ticks, spawns 5 entities, positions
  integrate correctly against drag (checked by hand — math matches).

## Build & run

Requires gcc (MinGW64 on Windows), make.

```bash
make all          # -> build/wubu_engine.exe
make run          # runs games/main.lua
./build/wubu_engine.exe games/yourgame.lua
```

## Layout

```
src/core/        engine core
  wb_ecs            ECS: sparse-set + SoA (opaque world handle)
  wb_spatial        quadkey lon/lat keying
  wb_tiles          tile store + LRU streaming
  wb_render         software 3D rasterizer
  wb_exp            experience containers (one Lua state + ECS each)
  wb_physics_*      MuJoCo: world / body / ECS binding
  wb_gpu*           D3D12 RHI + render graph: dev / buffer / texture /
                    shader / pass / exec
  wb_assets_*       content pipeline: glTF parse / DDC / manifest
  wb_avatar_*       avatar: bones / glTF load / anim sampling / identity
  wb_edit           in-world editor (ops, undo/redo, JSON replay)
  wb_dist           experience distribution (instances, matchmaking, portals)
  wb_util           shared helpers: file IO, hashing, error buffer, paths
  wb_gltf           shared glTF accessor decoding
src/lua/         Lua 5.4 host + bindings + modding sandbox
src/lv/          slerm-Lua interpreter: arena / value / lex / parse / eval / host
src/net/         networking: AOI / server / client / loopback + server meshing
thirdparty/      vendored deps (Lua 5.4, MuJoCo)
tests/           one harness per module; `make test` runs every gate
games/           Lua game scripts
docs/            design notes
```

## Module rules

- **Opaque structs.** Module state is a handle created by a `_new`/`_create`
  call (`wb_world_new()`, `wb_gpu_create()`, `wb_physics_new()`, ...). The
  struct definition lives in a private `*_internal.h` included only by that
  module's own `.c` files, so no caller can reach into it.
- **Minimal includes.** Include what you use. A header covers one concern --
  e.g. `wb_net.h` is the shared wire types + AOI, while the server, client and
  loopback transports have their own headers.
- **No god headers.** Nothing pulls in a whole subsystem to declare one type.
- **C11 only.** (The D3D12 backend is compiled as C++ behind `extern "C"`
  because MinGW's `d3d12.h` exposes C++ interfaces; the API stays C11.)
- **No duplicated helpers.** Shared code lives in `wb_util` (file IO, FNV-1a
  content hash, error buffer, path join) and `wb_gltf` (accessor decoding).
  Do not re-implement them per module.

## Roadmap (from the metaverse read)

1. ✅ ECS core + Lua host (this slice)
2. Geodata ingestion -> 1:1 Earth tiles
3. Ambient culling / sparse LOD streaming
4. 3D scene path (GPU on rig, sw raster on metal)
5. Experience spawning into world space (arena containers)
6. Slerm Lua (drop stock) to stay on the no-third-party pillar
7. Agentic connection to wubuwizard/federation

## Meta-engine assessment (2026-11-08)

`docs/meta-engine-assessment.md` — the strategic read: measured gaps, the five
platform pillars (networking, physics, content pipeline, avatar+identity,
creator tooling), the gameplay-loop taxonomy (core/secondary/meta) and the
targeted sandboxes (obby, tycoon, survival, BR, racing, social, build), the
competitive landscape (Roblox, UEFN, Minecraft, VRChat, Core, Vircadia,
Webaverse), and the reference architectures to copy (one spatial index for
AOI+streaming+sharding; authority-vs-interest; client prediction; Luau sandbox;
glTF+VRM avatars; content-addressed DDC). Ends with a prioritized slice 9–18
roadmap from kernel to meta engine.

## Contracts

- C11 owns memory + simulation; Lua owns logic. Lua never allocates core memory.
- Game rules/UI = Lua. Core loop, ECS, physics = C11.
