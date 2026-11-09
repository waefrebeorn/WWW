# WWW — the doctrine of the space

> **The internet was the World Wide Web. This is the World Wide WorldSpace.**

This document is the why. The code is the what. When they disagree, this one
wins.

## The argument

**Matter cannot be cloned, and nothing comes from nothing.** A physical good
is scarce because the universe is scarce. You can copy the *instructions* for
a chair infinitely; you cannot copy the chair.

Data and concepts do not have this property. They are infinitely reproducible
at zero marginal cost. Copyright and trademark are an emotional human
attachment layered on top of that fact — an attempt to *make something from
nothing* by decree, and a cult of existence around it, in the same family as
commerce and money.

**WWW does not care about money or security.** It operates as a *game space*.
Every dispute inside it is arbitrated by the real-world equivalent: you catch
the person. There is no DRM here, no licence server, no entitlement check, no
"platform trust" layer. The space simulates; it does not police.

This is not negligence. It is the point. A game space that does not pretend to
be a government is honest about what it is.

## Ready Player One, built from aggregation

The goal is a world space people **spawn into games** inside — the UEFN
instinct, but inverted:

| UEFN | WWW |
|------|-----|
| One engine, one platform, one economy | One space, many gametypes, no economy |
| You publish *to* Fortnite | You spawn *into* the world |
| Epic arbitrates | The real world arbitrates |
| Verse + devices | Lua + slerm-Lua + the editor |
| Islands are assets in someone else's catalogue | Gametypes are files in a directory you own |

The different thing — the thing that makes this not a clone of the platform
model — is **aggregation from nothing**. The space starts empty and is built
by slerming: taking a working system that exists, and reimplementing it from
the ground up inside the space, without forking. Every gametype that gets
spawned into the space is one more thing the space can do. The space grows by
accretion, and the accretion is *real* because each piece is a working
implementation, not a reference to someone else's.

## The gametype contract

A **gametype** is a declared, parameterisable package — the UEFN "project"
analogue, minus the platform:

```
gametype/
├── mode.json      # name, version, entry script, config schema, team count
├── main.lua       # the mode's step() — owns gameplay
└── assets/        # optional, referenced by path
```

- **Declared**: `mode.json` says what the mode is and what it needs.
- **Parameterisable**: `config` is a typed schema (int/float/bool/string with
  ranges and defaults). "Zone Wars 10v10" and "Zone Wars Solos" are the *same*
  gametype with different config — not two gametypes, not two scripts.
- **Spawned**: the space finds an instance for the mode, picks a spawn point,
  and puts the player in it. That is the whole flow: *pick a mode, get a
  world*.

The engine does not know what a "zone war" is. It knows: a mode has an entry
script, a config schema, a team count, and a set of spawn points. Everything
else is the mode's business. **That ignorance is what lets all gametypes work.**

## What we slerm next (the learned list)

The assessment's five pillars are built. The next tools come from the
aggregate — free, production-proven, reimplemented rather than vendored:

| Learned tool | What it gives | Slerm as |
|---|---|---|
| **Jolt Physics** (Death Stranding 2, Horizon FW) | rigid bodies, character controllers, broadphase | next physics backend (MuJoCo stays the reference) |
| **ozz-animation** | animation sampling, blending, skeletons | the avatar sampler's next cut |
| **Recast Navigation** | navmesh, pathfinding, crowds | `wb_nav` — NPCs that walk around each other |
| **meshoptimizer** | mesh simplify/compress | the content pipeline's DDC output stage |
| **FastNoise2** | procedural noise | terrain/zone generation for the tile store |
| **The Forge** | cross-platform graphics/asset layer | reference for the GPU backend's next revision |
| **Effekseer** | particle/FX | the FX pass in the render graph |
| **Terrain3D / Voxel Tools** | sculptable, diggable worlds | a future voxel terrain slice |
| **Material Maker** | procedural materials | the asset pipeline's material stage |

Rule: **read it, slerm it, do not vendor it.** A reference tree is fine; a
linked dependency is not. The no-third-party pillar holds.

## Principles

1. **The space is a material good.** It is scarce because it is built, not
   licensed. Nobody rents a gametype.
2. **No DRM, no anti-cheat, no platform trust.** The space simulates. The real
   world arbitrates.
3. **Aggregate as you go.** Every slice slerms something real. Nothing is
   planned that cannot be tested.
4. **Modes are ignorant; the engine is ignorant.** The engine knows modes,
   configs, spawns and instances. It does not know games.
5. **Opaque structs, minimal includes, C11, no duplicated helpers.** See the
   engine README.
