# WWW — World Wide WorldSpace

> **The internet was the World Wide Web. This is the World Wide WorldSpace.**
> The ~WuBu~ space keeps one truth and many copies; this repository is the
> space itself.

`WWW` is the world-space repository. Inside it, the ~WuBu~ space is the engine
that builds it — C11 owns memory, the ECS, physics and the spatial index; Lua
owns game logic, GUI and mods.

```
WWW/
├── README.md      this file
└── ~WuBu~/        the world-space engine
    ├── src/core/  ECS, quadkey spatial, tiles, render, exp, physics,
    │              GPU (D3D12), assets, avatar, editor, dist, gametypes
    │              (mode/spawn/worldspace), shared kernel
    ├── src/lua/   Lua 5.4 host + bindings + modding sandbox
    ├── src/lv/    slerm-Lua: the C11 interpreter (arena/value/lex/parse/eval/host)
    ├── src/net/   networking (AOI/server/client/loopback) + server meshing
    ├── tests/     one harness per module — `make test` runs every gate
    ├── games/     Lua game scripts
    ├── docs/      design notes + the meta-engine assessment
    └── thirdparty/ vendored Lua 5.4 + MuJoCo
```

## Build & run

```bash
cd '~WuBu~'
mingw32-make all      # -> build/wubu_engine.exe
mingw32-make test     # 20 gates, 705 assertions
mingw32-make run      # runs games/main.lua
./build/wubu_engine.exe games/yourgame.lua
```

Requires MinGW64 gcc (`mingw32-make`, not `make`) and, for the GPU and physics
gates, the Windows SDK / vendored MuJoCo.

## What lives where

| Thing | Where |
|-------|-------|
| the world space | this repository (`WWW`) |
| the engine inside it | `WWW/~WuBu~` |
| the registry — one truth, many copies | [WuBuFederation](https://github.com/waefrebeorn/WuBuFederation) |
| the OS it ships in | [WuBuOS](https://github.com/waefrebeorn/WuBuOS) |
| the shared kernel + glTF accessor layer | `WWW/~WuBu~/src/core/wb_util.*`, `wb_gltf.*` |

## Gametypes

A **gametype** is a declared, parameterisable package — the UEFN project
analogue, minus the platform:

```
gametype/
├── mode.json      name, entry script, typed config schema, teams
├── main.lua       the mode's step() — owns gameplay
└── assets/        referenced by path
```

`wb_worldspace` answers the one call a host makes: pick a mode, get an
instance and a world position. The engine knows modes, configs, spawns and
instances — it does not know games. That ignorance is what lets all gametypes
work. See `docs/WWW.md` for the doctrine of the space.

## Module rules (enforced)

- **Opaque structs.** Module state is a handle from a `_new`/`_create` call;
  the struct lives in a private `*_internal.h` included only by that module's
  own `.c` files.
- **Minimal includes.** One concern per header; nothing pulls in a subsystem to
  declare one type.
- **C11 only.** (The D3D12 backend is compiled as C++ behind `extern "C"`
  because MinGW's `d3d12.h` exposes C++ interfaces; the API stays C11.)
- **No duplicated helpers.** Shared code lives in `wb_util` and `wb_gltf`.
  Do not re-implement them per module.
