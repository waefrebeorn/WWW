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
src/core/        engine core (memory, ECS, physics)
src/lua/         embedded Lua host + bindings
thirdparty/      vendored deps (Lua 5.4)
games/           Lua game scripts
docs/            design notes
```

## Roadmap (from the metaverse read)

1. ✅ ECS core + Lua host (this slice)
2. Geodata ingestion -> 1:1 Earth tiles
3. Ambient culling / sparse LOD streaming
4. 3D scene path (GPU on rig, sw raster on metal)
5. Experience spawning into world space (arena containers)
6. Slerm Lua (drop stock) to stay on the no-third-party pillar
7. Agentic connection to wubuwizard/federation

## Contracts

- C11 owns memory + simulation; Lua owns logic. Lua never allocates core memory.
- Game rules/UI = Lua. Core loop, ECS, physics = C11.
