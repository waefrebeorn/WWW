# WORKLIST — the research, organized for execution

> `LILYPADS.md` is the map; this is the route. Every row is one **lilypad** — one
> crossable deliverable with a gate on the far side — ordered by **dependency**
> (hop-to-hop), each with its best source and status. Nothing is scheduled by
> date; the order *is* the schedule. Source ids resolve in `LILYPADS.md`'s
> Sources block / the shared citation ledger.

## Status

- **SHIPPED** — real, gated code exists in `~WuBu~` (see the gate list).
- **PARTIAL** — a stone of it is shipped; the remainder is named.
- **TODO** — not started.

## The frontier (next hops)

1. **LP-03 / LP-04 completion** — wire the per-tick digest into the physics step
   (the sim's own integration feeds `wb_snap_digest_world`) and add render
   interpolation on the fixed accumulator. Closes the determinism loop
   end-to-end.
2. **LP-06 job system** — the scheduler everything in Wave 2 is shaped like.
3. **LP-87 desync triage** — makes the LP-03 checksum diagnosable, not just
   detectable (pairs with the frontier).

Then LP-02/LP-05, and Wave 2 (render) opens.

---

## P0 — FOUNDATION (measure + simulate)

### INSTRUMENT

| pad | work item | size | depends on | best source | status |
|---|---|---|---|---|---|
| LP-01 | Profiling, telemetry, statistics ring buffer | M | — | [147][567] | **PARTIAL** (counters/zones/ring/budget/percentiles shipped) |
| LP-02 | Memory architecture (tagged heap, per-thread blocks, frame allocators) | L | LP-01 | [18][13] | TODO |
| LP-05 | Performance & memory budgets (budget-fails-build gate) | M | LP-01, LP-02 | [559][564] | **PARTIAL** (budget mechanism shipped) |
| LP-47 | Engine testing & CI (replay-based regression) | L | LP-03 | [562][566] | TODO |
| LP-48 | Large-team source-control workflow | M | — | [245] | TODO |

### SIMULATE

| pad | work item | size | depends on | best source | status |
|---|---|---|---|---|---|
| LP-03 | Determinism suite (PRNG, checksum, fixed-point, trig, per-tick digest, rollback) | L | LP-01 | [54][541][542] | **PARTIAL** (substrate shipped; toolchain flags = LP-91) |
| LP-04 | Fixed-timestep sim + render interpolation | M | LP-03 | [540] | **PARTIAL** (accumulator shipped; interpolation not) |
| LP-06 | Job system (fibers/work-stealing, counters) | L | LP-01, LP-07 | [5][552] | TODO |
| LP-07 | Data-access validation (safe-point resource tracking) | M | LP-06 | [6][18] | TODO |
| LP-08 | SIMD, cache-conscious layout, memory ordering | M | LP-06 | [554][548] | TODO |
| LP-10 | Geometry optimization pass (vertex cache, meshlets) | M | LP-08 | [322][318] | TODO |

### New — INSTRUMENT + SIMULATE (third-degree)

| pad | work item | size | depends on | best source | status |
|---|---|---|---|---|---|
| LP-87 | Desync prevention & triage tooling | L | LP-03 | [562][566][54] | TODO |
| LP-88 | Causal profiling (Coz) | M | LP-01 | [545][571] | TODO |
| LP-89 | End-to-end latency measurement (input-to-photon) | M | LP-01, LP-32 | [557][556] | TODO |
| LP-90 | Statistical benchmarking methodology | M | LP-05 | [559][564][565] | TODO |
| LP-91 | Compiler/toolchain determinism flags & static audits | M | LP-03 | [562][151] | TODO |
| LP-92 | Memory measurement practice (alloc tail latency) | M | LP-02 | [563][5][13] | TODO |

---

## P1 — VISIBLE (render)

| pad | work item | size | depends on | best source | status |
|---|---|---|---|---|---|
| LP-09 | GPU memory management (suballocation, placed resources, aliasing) | M | LP-06 | [532] | TODO |
| LP-11 | Bindless resource binding | M | LP-09 | [88] | TODO |
| LP-12 | Material system | L | LP-11 | [401] | TODO |
| LP-13 | Texture streaming & GPU-compressed textures | L | LP-09 | [532] | TODO |
| LP-14 | Occlusion culling | M | LP-16 | [522] | TODO |
| LP-15 | LOD & HLOD | L | LP-10 | [523] | TODO |
| LP-16 | GPU-driven rendering | XL | LP-06, LP-11, LP-14 | [42] | TODO |
| LP-17 | Mesh shaders & virtualized geometry | XL | LP-16, LP-10 | [401] | TODO |
| LP-18 | Temporal upscaling | M | LP-12 | [40] | TODO |
| LP-19 | GPU particles | L | LP-16 | [208] | TODO |
| LP-20 | Real-time global illumination | L | LP-12 | [531][529] | TODO |
| LP-21 | Shader build dependency tracking | M | LP-12 | [524][527] | TODO |
| LP-60 | GPU synchronization & resource-state/barrier management | L | LP-09 | [525][527][524][518] | TODO |
| LP-61 | HDR / color management / tone mapping | M | LP-12 | [521] | TODO |
| LP-62 | Virtual texturing as a residency/feedback system | L | LP-09, LP-11 | [526][520][537] | TODO |
| LP-63 | Reflection-tier design (SSR → RT → hybrid) | M | LP-12, LP-20 | [536][531][529] | TODO |
| LP-64 | Multithreaded draw submission | L | LP-06, LP-16 | [532][534][518] | TODO |
| LP-65 | Multi-GPU / multi-adapter RHI awareness | L | LP-09 | [528][532] | TODO |

---

## P2 — ALIVE (animate + emit)

| pad | work item | size | depends on | best source | status |
|---|---|---|---|---|---|
| LP-22 | Animation blending, state machines, IK | L | LP-12 | [71][72] | TODO |
| LP-23 | Animation retargeting & humanoid bone mapping | M | LP-22 | [504] | TODO |
| LP-24 | Navmesh & pathfinding | M | LP-10 | [502][503] | TODO |
| LP-27 | Animation-driven locomotion | M | LP-22 | [500] | TODO |
| LP-25 | Audio engine | L | LP-06 | [39][40] | TODO |
| LP-26 | Spatial audio | M | LP-25 | [43][41] | TODO |
| LP-66 | Animation compression (ACL) | L | LP-22 | [498][499][82] | TODO |
| LP-67 | Motion matching / data-driven animation | L | LP-22, LP-66 | [500] | TODO |
| LP-68 | Crowd / SIMD / GPU-batched skinning | L | LP-08, LP-22 | [501][502] | TODO |
| LP-69 | Physics-blended & procedural animation | M | LP-22 | [514] | TODO |

---

## P3 — TOGETHER (connect)

| pad | work item | size | depends on | best source | status |
|---|---|---|---|---|---|
| LP-28 | Delta compression | M | LP-03 | [512] | TODO |
| LP-29 | Snapshot interpolation | S | LP-28 | [505] | TODO |
| LP-30 | Lag compensation | M | LP-28 | [506] | TODO |
| LP-31 | Rollback netcode | L | LP-03, LP-04, LP-28 | [148][66] | TODO |
| LP-32 | Network debugging & profiling tooling | M | LP-28 | [509] | TODO |
| LP-33 | Server meshing control plane | L | LP-31 | [508] | TODO |
| LP-34 | Matchmaking & parties | L | LP-33 | [510] | TODO |
| LP-35 | Anti-cheat & client attestation | L | LP-31, LP-71 | [425][516] | TODO |
| LP-70 | Transport & reliability layer (reliable-UDP, encryption) | L | LP-28 | [507] | TODO |
| LP-71 | Interest management beyond the quadkey (variable AOI, priority, visibility) | L | LP-31 | [511][507][516] | TODO |
| LP-72 | Headless dedicated-server simulation & netcode QoS telemetry | L | LP-32, LP-33 | [517][515][509] | TODO |

---

## P4 — CREATABLE (author)

| pad | work item | size | depends on | best source | status |
|---|---|---|---|---|---|
| LP-36 | Asset pipeline as a graph | L | LP-09 | [481][84] | TODO |
| LP-37 | Import-time shader processing | M | LP-12 | [96] | TODO |
| LP-38 | Shader graphs & material editor | L | LP-12 | [232][274] | TODO |
| LP-39 | Visual scripting for designers | L | LP-36 | [233] | TODO |
| LP-40 | Cinematics & sequencing | L | LP-22 | [482] | TODO |
| LP-41 | VFX authoring | L | LP-19 | [213][208] | TODO |
| LP-42 | UI system (widgets, text shaping, one-draw-call) | XL | LP-12 | [189][198] | TODO |
| LP-43 | Terrain system | XL | LP-13, LP-15 | [202][289] | TODO |
| LP-44 | DCC tools pipeline | L | LP-36 | [485][484] | TODO |
| LP-45 | Designer-facing data tooling | L | LP-36 | [486] | TODO |
| LP-49 | Platform & certification engineering | M | LP-46 | [236] | TODO |
| LP-73 | Sandboxed hot-reload reliability | M | LP-36 | [488] | TODO |
| LP-74 | Editor undo / transaction / command system | L | LP-36 | [491][489] | TODO |
| LP-75 | Importer-versioned DAG asset builds + build-key correctness | L | LP-36 | [481][490] | TODO |
| LP-76 | Cloud / dedicated build farms | L | LP-36 | [487] | TODO |
| LP-77 | Multi-user co-authoring | XL | LP-74 | [483] | TODO |
| LP-78 | Asset validation / lint pipeline | L | LP-36 | [490][492] | TODO |
| LP-79 | Asset provenance & licensing | L | LP-36 | [490][492][493] | TODO |

---

## P5 — HOSTED (platform)

| pad | work item | size | depends on | best source | status |
|---|---|---|---|---|---|
| LP-46 | Build, cook & deploy pipeline | L | LP-36 | [468] | TODO |
| LP-50 | Persistence layer | XL | LP-33 | [478] | TODO |
| LP-51 | Identity & social graph | L | LP-33 | [478] | TODO |
| LP-52 | Discovery & curation | L | LP-50 | [pass 1] | TODO |
| LP-53 | Content moderation & safety | XL | LP-50 | [pass 1] | TODO |
| LP-54 | Age gating & legal posture | L | LP-50 | [472] | TODO |
| LP-55 | Creator economy & publishing | XL | LP-50 | [477] | TODO |
| LP-56 | Interoperability & federation | XL | LP-50 | [pass 1] | TODO |
| LP-57 | AI-generated content & validation | XL | LP-78 | [pass 1] | TODO |
| LP-58 | Platform live ops | L | LP-50 | [475] | TODO |
| LP-59 | Accessibility & localization | L | LP-42 | [pass 1] | TODO |
| LP-80 | Multi-tenant tenancy & per-gametype isolation | L | LP-33 | [467][468][497] | TODO |
| LP-81 | Service-to-service security / rate limiting | L | LP-50 | [470] | TODO |
| LP-82 | Offline / edge caching & global distribution | M | LP-46 | [471] | TODO |
| LP-83 | Data portability / GDPR / privacy posture | L | LP-50 | [472][495][475] | TODO |
| LP-84 | Availability / SLO engineering & on-call | L | LP-50 | [473][496] | TODO |
| LP-85 | Real-money economies & in-game purchase compliance | L | LP-55 | [480][494][472] | TODO |
| LP-86 | Runtime plugin/module & avatar-item marketplace | XL | LP-52 | [476][477] | TODO |

---

## Dependency forks (why this order)

- **Determinism (LP-03) before rollback (LP-31)** — spend the cost in P0, cash it in P3.
- **Jobs (LP-06) before GPU-driven + mesh shaders (LP-16/17)** — those are compute-shaped.
- **The build pipeline (LP-46) the moment there are two machines** — not when P5 starts.
- **Platform concerns stubbed at the door they gate** — matchmaking (LP-34) with the first
  multiplayer game; accessibility (LP-59) with the first shipped gametype.
- **LP-75 (build key) is the fix for the known DDC staleness** — do it with LP-36.

## Wave 7 at a glance (33 third-degree pads)

RENDER: LP-60..65 · ANIMATE/CONNECT: LP-66..72 · AUTHOR: LP-73..79 · HOST: LP-80..86 · INSTRUMENT/SIMULATE: LP-87..92.

## Research corpus

Reports `00..18-*.md` in the research scratch; `LILYPADS.md` for full pad detail and
the Sources block; the shared citation ledger (~571 sources) for every URL.
