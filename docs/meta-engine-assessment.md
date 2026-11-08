# Meta-Engine Assessment — Gaps, Needs, Gameplay Loops & Targeted Sandboxes

**Date:** 2026-11-08
**Author:** Hermes (OWL)
**Inputs:** local repo measurement + ~20 web searches (Roblox/Luau, UEFN, Minecraft,
VRChat/Core/Rec Room/Second Life, SpatialOS/server meshing, glTF/VRM/OMI, physics
engines, render graphs, persistence, audio, asset pipelines, genre templates).
**Purpose:** the strategic input for "we will be making the best meta engine."

---

## 0. TL;DR

The engine has a **real, tested foundation** (ECS + quadkey spatial + tile LRU +
software raster + experience containers + two script hosts) but is a
**single-player simulation kernel**, not yet a meta engine. The gap is not "more
features" — it is **five platform pillars** that turn a simulation kernel into a
meta engine:

1. **Networking** (server-authoritative + prediction + interest management)
2. **Physics** (collision + character controller + rigid bodies)
3. **Content pipeline** (glTF/VRM in → GPU-ready out, content-addressed)
4. **Avatar + identity layer** (the cross-world identity that makes it "meta")
5. **Creator tooling + modding API** (the reason people build on it)

Everything else (audio, UI, persistence, editor) is downstream of those five.

---

## 1. Current state — measured, not claimed

```
src/core/wb_ecs.c|h        sparse-set + SoA ECS          21 tests
src/core/wb_spatial.c|h    quadkey lon/lat spatial      142 tests
src/core/wb_tiles.c|h      tile store + LRU             49 tests
src/core/wb_render.c|h     software 3D rasterizer        6 tests
src/core/wb_exp.c|h        experience containers        15 tests
src/lua/wb_script.c|h      Lua 5.4 host + bindings       (engine runs)
src/lv/wb_lv.c|h           slerm-Lua tree-walker     17+5 tests
                           -------------------------------------
                           2,318 LOC · 255 assertions · 0 fail
```

**What is genuinely good and should not be rebuilt:**
- **The quadkey spatial core is the right spine.** Research converges on one
  insight: *one spatial index serves three jobs* — interest management (who
  sees whom), streaming (what the client loads), and shard boundaries (who
  owns). The engine already has that spine (`wb_spatial` + `wb_tiles`). This is
  the same architecture SpatialOS / Star Citizen / open-mmorpg describe.
- **ECS is data-oriented and tested** — the right shape for a simulation kernel.
- **Two script hosts** — stock Lua 5.4 as the placeholder, slerm-Lua as the
  owned C11 substrate. Correct call: the scripting layer is the "C# replacement"
  and the modding surface.
- **Experience containers** — the "Yu-Gi-Oh stadium" instinct (N instances = N
  worlds) is the right primitive for a meta engine.

**What is missing is the platform, not the kernel.**

---

## 2. Gap analysis — five pillars

### Pillar 1 — Networking (the biggest gap)
The engine has zero networking. A meta engine is *definitionally* multiplayer
and persistent. Required, in order:

| System | What it is | Reference architecture |
|---|---|---|
| Authoritative server | server simulates truth; clients send inputs | UE GameMode/GameState/PlayerState split; Unity Netcode |
| Client prediction + reconciliation | tick-stamped inputs, ring buffer, rewind+replay | standard (Gabriel Gambetta model) |
| Interest management (AOI) | only replicate what a client can perceive | quadtree/quadkey cells ≈ view radius; priority self>combat>near>far |
| Delta compression | send changes vs last-acked baseline | quantization before checksum/predict-compare |
| Server meshing | many servers, one seamless world, authority handover | SpatialOS `NotAuthoritative→Authoritative→AuthorityLossImminent→NotAuthoritative`; Star Citizen Replication Layer + Persistent Entity Streaming |
| Lag compensation | rewinds hitboxes for shooters | standard |

**Key numbers from research:** server tick 10–30 Hz (MMO) / 60+ (shooter);
client→server input packets 10–50 bytes; server→client budget 20–50 KB/s per
client; spatial hashing ~30× faster than all-pairs; bandwidth O(nearby) not
O(world). **The engine's quadkey core is already the right AOI backbone** — the
netcode should be built *on* it, not beside it.

### Pillar 2 — Physics
The ECS has position/velocity/mass but **nothing integrates against world
geometry** — no broadphase, no narrowphase, no character controller. Options:

| Engine | Lang | Notes |
|---|---|---|
| **Jolt Physics** | C++ | AAA (Horizon FW), character controller, vehicles, soft bodies, SIMD, WASM multithread. Best feature set. |
| **Rapier** | Rust→WASM/C API | Deterministic (same inputs = same outputs), character + vehicle controllers, CCD. Best for lockstep/replay. |
| **PhysX** | C++ | Mature, vehicles, but heavier. |
| **Write own** | C11 | Only for a minimal capsule-vs-mesh character controller + sphere/box broadphase. |

**Recommendation:** Jolt for the rig (feature completeness) *or* Rapier (C API +
determinism, which matters for replays/netcode). Do not write a full physics
engine — that is a multi-year detour. A minimal own character controller
(capsule, slopes, stairs, moving platforms) is a reasonable first slice *if*
networking is deferred.

### Pillar 3 — Content pipeline
No asset import at all. Required:
- **glTF 2.0 / GLB in** (meshes, materials, skins, animations) — the open
  standard; "JPEG of 3D"; ISO standard.
- **KTX2/Basis textures** → transcode to GPU block format (BCn/ASTC/ETC2).
- **Content-addressed derived-data cache (DDC)** — build outputs keyed by
  source hash + build key; incremental builds never trust timestamps.
- **Manifest + CDN** — cryptographic manifest as source of truth; differential
  patching; edge caching.
- **Hot reload** — file-watcher live reload for the iterate-observe loop.
- **LOD chains + HLOD proxies** — decimated tiers, imposters, merged distant
  clusters (Unreal HLOD concept).

### Pillar 4 — Avatar + identity layer (the "meta" in meta engine)
This is what separates a game engine from a meta engine: **identity and assets
that persist across worlds.** Research is unambiguous:
- **glTF** for all geometry; **VRM** (built on glTF 2.0) for humanoid avatars —
  normalized humanoid bone mapping (Hips/Spine/Head/arms/legs), blend shapes,
  **spring bones** for hair/cloth physics, camera viewpoints, licensing.
- **Khronos + VRM Consortium** are converging glTF avatar extensions (2024–2026);
  Meta, Roblox, Linden Labs are at the table. **Build on glTF+VRM, not a
  proprietary avatar format.**
- **OMI (Open Metaverse Interoperability Group)** — protocols for identity,
  social graphs, inventory across virtual worlds. W3C community group.
- **Metaverse Standards Forum** — the coordinating body; glTF is the foundational
  ISO standard, extensions for geospatial, physics, interactivity, avatars.

### Pillar 5 — Creator tooling + modding API
The engine's *reason to exist* is "people make the best mods and slerm other
games into our world space." Required:
- **A modding API** that is safe for untrusted code (see §4 security).
- **An editor** (Roblox Studio equivalent) — in-engine world editing, live edit,
  hot reload. This is a large build and a differentiator.
- **A distribution surface** — the "world space and distribution system that
  boots up in WuBuOS." Marketplace/portal/discovery.
- **Sandboxing** — untrusted creator code must not escape (see §4).

---

## 3. Gameplay loops — the taxonomy to design for

Research (Sellers' loop taxonomy, gdad.wiki, Sandbox.game docs, genre templates)
converges on a **three-loop hierarchy** every game (and therefore every
*game-creation platform*) must support:

| Loop | Timescale | What it is | Platform implication |
|---|---|---|---|
| **Core loop** | seconds–minutes | the action the player does most (shoot, jump, build, bid) | must be scriptable + tunable by creators |
| **Secondary loop** | minutes–hours | progression, upgrades, economy, unlocks | needs persistence + economy systems |
| **Meta/social loop** | days–months | guilds, seasons, leaderboards, live events, UGC | needs identity + social graph + live-ops |

**Inner vs outer loops:** inner = reaction/feedback (combat, movement); outer =
planning/progression (tech trees, base building, deck building). A meta engine
must let creators define *all three* per experience.

### Genre-specific loops (the "targeted sandboxes")
From UEFN/Roblox genre research, the proven sandbox templates a meta engine should
target first:

| Sandbox | Core loop | Retention hook | Difficulty |
|---|---|---|---|
| **Obby / parkour / deathrun** | jump, dodge, reach checkpoint, retry | "one more try" + speedrun | Beginner (geometry + checkpoints) |
| **Tycoon** | earn → buy droppers/machines → expand plot | visible progress + next unlock | Intermediate (currency + upgrades + saved progress) |
| **Simulator / clicker** | tap/collect → sell → buy upgrade | faster collection | Intermediate |
| **Survival / crafting** | gather → craft → build base → fend off threats | goal-chaining + shared tension | Advanced (inventory, crafting, AI) |
| **Battle royale / box fights / zone wars** | drop → loot → fight → last standing | competition + skill expression | Intermediate–Advanced |
| **Racing / stunt** | drive → drift → finish → upgrade | mastery + tracks | Intermediate (vehicle physics) |
| **Prop hunt / tag / mini-game hub** | hide/seek → rotate → party | variety (one bad round, next is fine) | Intermediate |
| **Horror / escape room / roleplay** | explore → solve → survive / act | story + atmosphere | Advanced |
| **Social hangout** | meet → chat → emote → events | presence + community | Intermediate (voice + avatars) |
| **Sandbox build (voxel)** | place/break blocks → save creation | creative expression + UGC | Beginner–Intermediate |

**The design rule the research repeats:** define, per genre — first 30 seconds,
core loop, progression ladder, social hook, monetization fit, update cadence,
mobile control risks, security-sensitive systems. A meta engine should ship
**these as templates** (the "targeted sandboxes"), not as one generic toolkit.

---

## 4. Targeted sandboxes — the competitive landscape

| Platform | Engine | Scripting | UGC model | Self-hostable? | Key strength |
|---|---|---|---|---|---|
| **Roblox** | custom C++ | Luau (typed Lua) | Studio + Cloud + economy | No | the flywheel: engine + Studio + cloud + Robux + social graph |
| **Fortnite / UEFN** | Unreal 5 | Verse + devices | Creative + UEFN + Fab | No | AAA tooling + 40% revenue share |
| **Minecraft** | custom | mods (Forge/Fabric) + plugins (Bukkit/Spigot/Paper) | vanilla + modded ecosystem | Yes (server) | the modding lineage: client mods + server plugins |
| **VRChat** | Unity | Udon / SDK | avatar + world upload | No | avatar-centric social VR, largest creator community |
| **Core** | custom | Lua | in-game editor + Circuits | No | in-game creation + visual scripting |
| **Rec Room** | Unity | Circuits | Maker Pen + rooms | No | in-game creation, cross-platform |
| **Second Life** | custom | LSL | in-world edit + attachments | No | persistent economy + land |
| **Vircadia** | custom (High Fidelity lineage) | JS | federated domain servers | **Yes** | self-hostable, federated, no central registry |
| **Webaverse / XREngine** | three.js / node | JS | open standards (glTF/VRM) | **Yes** | open source, self-hostable, web-first |
| **Godot** | Godot | GDScript/C# | open engine | n/a | open source, scene tree + server layer |

### What the winners do that a challenger must match
1. **Engine + tooling + distribution are one product.** Roblox is not an engine;
   it is engine + Studio + cloud + economy + social graph. The a16z thesis is
   explicit: *engines become platforms, games are wrapped and hosted within the
   engine.* The wubu-engine's "boots up in WuBuOS" is the right instinct — the
   OS is the distribution surface.
2. **Untrusted code is a first-class problem.** Luau's sandbox is the reference:
   - no bytecode loading from untrusted source (memory-safety);
   - per-script global tables (isolation between scripts);
   - separate VMs for trusted vs untrusted code;
   - watchdog interrupts (10s script limit, 1s shutdown);
   - bytecode signed/encrypted across network or filesystem.
   **The engine needs a sandbox story before it has modders.**
3. **Open formats beat walled gardens.** a16z: the metaverse needs *universal
   content formats* and *composability* — components of one game working in
   another. glTF + VRM + OMI are the open path; proprietary formats are the
   Roblox/UEFN moat that a challenger should *not* copy.
4. **Composability is the differentiator.** "Pokemon hunting in the GTA
   universe" — items/abilities/avatars that cross experiences. The engine's
   experience containers + a shared asset/inventory layer is the right shape.

---

## 5. Technical architecture — reference models to copy

### 5.1 One spatial index, three jobs
> "One spatial index serves three jobs: interest management (who sees whom),
> streaming (what the client loads), and shard boundaries (who a shard owns)."
— open-mmorpg spec

**The engine already has this.** `wb_spatial` (quadkey) + `wb_tiles` (LRU) is
the right backbone. The netcode and streaming should both read it. Do not fork a
second index.

### 5.2 Authority vs interest separation (SpatialOS)
Write-access **authority** over an entity-component is distinct from **interest**
(read/query). One worker simulates; many can read. Per-component authority
(position owned by physics worker, inventory by another) enables fine-grained
load balancing. Authority handover is a state machine with an
`AuthorityLossImminent` callback so the losing server flushes critical updates.

### 5.3 Client prediction + reconciliation (the standard model)
1. Client samples input, stamps with sequence/tick, sends it.
2. Client applies input locally to a predicted copy, renders immediately.
3. Client keeps `(tick, input, predictedState)` in a ring buffer.
4. Server simulates, returns authoritative state stamped with last consumed tick.
5. Match within epsilon → discard history. Mismatch → rewind to server state,
   replay all inputs after that tick.
**Quantize before checksumming and before prediction-compare** or quantization
itself causes false mispredicts.

### 5.4 Sandbox untrusted code (Luau model)
- Remove unsafe stdlib features; add VM features for isolation.
- Per-script global tables via `__index` to a builtin global table.
- Separate VMs for trusted/untrusted code.
- Watchdog interrupts (runtime limits, shutdown deadlines).
- Sign/encrypt bytecode at rest and in transit.

### 5.5 Content pipeline (open-mmorpg model)
- **glTF in → KTX2/meshlet out**, no proprietary converter.
- Content-addressed DDC; incremental build keyed by source hash + build key.
- Manifest-driven asset slots; AI-generated content validated against slot spec.
- Camera-position streaming by the same quadtree the server uses for AOI.
- Hot reload via file-watcher.

### 5.6 Render graph (Godot/Vulkan model)
- DAG of render passes; nodes = passes, edges = dependencies.
- Automatic barriers, transient resource aliasing, async-compute scheduling.
- Abstract around Vulkan 1.3/1.4 concepts that map to D3D12: dynamic rendering,
  synchronization2, buffer device address, descriptor indexing, indirect
  commands, timeline semaphores.
- The **software rasterizer is the null/headless backend** — keep it as the
  portable floor and the test harness, add GPU behind the same geometry API.

### 5.7 Persistence (MMO model)
- Simulation nodes never write to the DB directly — a central persistence
  service caches live entities in memory and flushes on a schedule.
- Object layer (game-agnostic serialized blobs) + attribute layer (mutable
  attributes in typed tables for fast queries).
- Event-based writes (on state change) + snapshots.
- **Persistent Entity Streaming**: a universe-scale DB storing every object's
  state so the world persists across sessions and server restarts.

---

## 6. Prioritized roadmap — from kernel to meta engine

**Slice 9 — Physics + character controller.** The kernel can't be played without
it. Minimal: capsule character controller (slides, slopes, stairs, moving
platforms) + sphere/box broadphase + narrowphase. Either integrate Jolt/Rapier
or write the minimal own controller. *Exit: a Lua script can spawn a character
and walk it around a tile plate.*

**Slice 10 — Input + camera + a playable character.** Input abstraction
(kb/mouse/gamepad/touch), a follow camera, and the character controller wired to
input. *Exit: a playable first-person/orbital character in a streamed tile world.*

**Slice 11 — GPU render backend.** Render graph + Vulkan (or D3D12) behind the
same geometry API the software rasterizer uses. *Exit: the tile world rendered
on the GPU at interactive framerates.*

**Slice 12 — Asset pipeline.** glTF/GLB import + KTX2 textures + DDC + manifest.
*Exit: a glTF model placed in the world, streamed by the quadkey store.*

**Slice 13 — Networking core.** Authoritative server + input replication +
client prediction + AOI on the quadkey. *Exit: two clients see each other move
in one world.*

**Slice 14 — Avatar + identity.** glTF+VRM avatar import, humanoid rig,
animation, cross-world identity. *Exit: a persistent avatar that walks between
two experiences.*

**Slice 15 — Modding API + sandbox.** Untrusted-code sandbox (Luau model),
mod manifest, hot reload. *Exit: an untrusted mod runs, is sandboxed, and can't
escape.*

**Slice 16 — Experience spawning + distribution.** Portals/stadiums, instance
matchmaking, discovery, marketplace hooks. *Exit: a player walks into a stadium
and lands in a different experience.*

**Slice 17 — Editor.** In-engine world editing + live edit. *Exit: a creator
builds a small experience without leaving the engine.*

**Slice 18 — Server meshing.** Authority handover, per-component authority,
persistent entity streaming. *Exit: a world that spans multiple servers
seamlessly.*

---

## 7. Honest risks

- **Scope.** This is a multi-year build. The five pillars are each a major
  system. The discipline that got the kernel to 255 green assertions — small
  slices, real tests, no overclaiming — is the only thing that will get the
  platform there.
- **Networking is the make-or-break.** A single-player meta engine is a
  contradiction. It must be designed in from slice 13, not bolted on.
- **The sandbox is a security boundary, not a feature.** One escape in a
  modding platform ends the platform. Budget for it early.
- **Content is the moat.** Roblox/UEFN win on content + distribution, not on
  engine quality. The engine must make content *cheap* (templates, glTF, AI-gen
  slot validation) or it will not attract creators.
- **The software rasterizer is a floor, not the product.** It is the right
  portable/test backend, but the GPU path is what makes a meta engine visual.

---

## Appendix — research sources (selected)

- a16z, *Unlocking the Metaverse* — engines become platforms; composability +
  interoperability; universal content formats.
- Roblox engineering (techinterview.org, devforum) — Luau, ECS, real-time
  networking, server allocation, creator economy.
- Luau sandbox docs — memory safety, per-script globals, watchdog interrupts,
  bytecode signing.
- SpatialOS / SparkEngine wiki — authority vs interest, per-component authority,
  authority handover state machine.
- Star Citizen (Object Container Streaming, Server Meshing, Persistent Entity
  Streaming) — production-grade state-server decoupling.
- open-mmorpg spec — one spatial index for AOI + streaming + sharding; quadtree
  is the single authority.
- Khronos glTF / VRM Consortium — glTF as ISO standard; VRM humanoid avatars;
  convergence 2024–2026.
- OMI / Metaverse Standards Forum — open protocols for identity, social graphs,
  inventory.
- Godot architecture + Vulkan talks — scene/server/core layers; render graph.
- Physics comparisons (Jolt/Rapier/PhysX) — Jolt for features, Rapier for
  determinism.
- Persistence (McGill, Photon, EPM) — object+attribute layers, central
  persistence service, event-based writes.
- Genre templates (UEFN, Roblox genre patterns, game-development-orchestrator) —
  core/secondary/meta loops; per-genre sandbox design.
