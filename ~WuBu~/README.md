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
mingw32-make test       # 17 gates, 573 assertions
mingw32-make run        # runs games/main.lua
./build/wubu_engine.exe games/yourgame.lua
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
| `src/core/wb_util.*` | shared helpers: file IO, FNV-1a hash, error buffer, paths |
| `src/core/wb_gltf.*` | shared glTF accessor decoding + GLB splitting |
| `src/lua/` | Lua 5.4 host + `wb.*` bindings + the modding sandbox |
| `src/lv/` | slerm-Lua: the C11 interpreter (arena/value/lex/parse/eval/host) |
| `src/net/` | networking: AOI / server / client / loopback + server meshing |
| `tests/` | one harness per module |
| `games/` | Lua game scripts |
| `docs/` | design notes + the meta-engine assessment |

## Status

All 18 roadmap slices are done — ECS, spatial, tiles, render, experience
containers, slerm-Lua, physics, input/camera/character, GPU, content pipeline,
networking, avatar/identity, modding sandbox, distribution, editor, server
meshing. `docs/meta-engine-assessment.md` is the strategic read: the five
platform pillars and the slice roadmap.

## Module rules

- **Opaque structs.** Module state is a handle from a `_new`/`_create` call;
  the struct lives in a private `*_internal.h` included only by that module's
  own `.c` files.
- **Minimal includes.** One concern per header.
- **C11 only.** The D3D12 backend is compiled as C++ behind `extern "C"`
  because MinGW's `d3d12.h` needs it; the API stays C11.
- **No duplicated helpers.** Shared code lives in `wb_util` and `wb_gltf`.
