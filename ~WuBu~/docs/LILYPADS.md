# LILYPADS — the hop list

> A **lily pad** is a small, crossable deliverable: one hop from where the engine
> stands now, with a gate on the far side. Pads are ordered so each one lands
> somewhere you can stand. Nothing here is a promise; each is a slice.
>
> Research: 2026-11-09. Two passes — the GDC corpus, the engine blogs, and
> current engine releases; then a second-degree ("Kevin Bacon") traversal that
> followed their links into talks, posts and platform docs the first pass never
> touched. Sources are numbered; the block at the end is generated from the
> citation ledger, not typed.

## Standing rules

- Every pad ships a real gate. No gate, no pad.
- Pads are **ordered by dependency**, not by importance.
- **Wave 0 first.** You cannot optimize what you cannot see, and you cannot
  reconcile a netcode on a nondeterministic simulation.
- Reject vendoring: read it, slerm it, do not link it.
- The engine stays ignorant of games. A pad that makes the engine know what a
  "zone war" is, is a bad pad.

---

## Wave 0 — measure and stabilize

### LP-01 — Profiling, telemetry, and a statistics ring buffer
Zone macros in the loop, physics, render graph and netcode; a Tracy/Optick-class
hybrid frame+sampling view; GPU timestamps; memory allocation tracking. Add a
frame-accumulated ring-buffer counter with a pointer-to-accumulator hot path so
hitches and unbounded-growth leaks are visible without a profiler attached.
Instrumentation gives exact per-scope timing but only for marked code, so the
markers go in **first** — retrofitting them later is what everyone regrets.
*[147][150][155][306]* · **Size M** · Unlocks evidence for every later pad.

### LP-02 — Memory architecture
**The substrate is the HIVE, not a FIFO ring** (slermed from wubuwizard/slermes
`wubu_hive.c`): linked fixed blocks + skipfield + freelist — stable slot
addresses, O(1) amortized insert, O(1) erase by slot, cache-friendly iteration,
no compaction. Tagged-lifetime heap with 2 MiB blocks and no `free(ptr)`;
per-thread blocks so no locks are needed for 99.9% of allocations; per-thread
frame allocators; double-buffered GPU scratch; allocation sentinels and tracing.
Naughty Dog removed 100–200 MiB of wasted worst-case linear allocators this way.
*[5][13][14]* · **Size L** · Unlocks streaming scale and multithreading.

### LP-03 — Determinism suite
A Q-format fixed-point type for simulation, physics, collision and navigation;
compiled trig LUTs; a fixed sim rate chosen for exact delta time (64 Hz, not
60); seeded in-state PRNG; stable container iteration order; per-tick checksums;
a one-frame-rollback sync test that compares re-simulated state. Photon Quantum
replaces every `float`/`double` with Q48.16 for exactly this. *[148][149][151]*
· **Size L** · Unlocks rollback, replay, and mesh authority verification. MuJoCo's
CPU backend is already deterministic *[139][140]*; the rest of the simulation
is not.

### LP-04 — Fixed-timestep simulation with render interpolation
A discrete fixed-step sim independent of render refresh, interpolated visual
transforms, and cosmetic state (VFX, audio, particles) isolated from game
state. The sim never reads a clock. Variable render-driven steps break the one
guarantee LP-03 builds on. *[151]* · **Size M** · Prerequisite for LP-03 and LP-31.

### LP-05 — Performance and memory budgets
A budgeting methodology: per-system CPU/GPU milliseconds, per-world entity and
triangle ceilings, per-stream VRAM and load-time targets, and a test that fails
when a budget is blown. Streaming today is a cache, not a budget — nothing says
what a world is allowed to cost. *[267]* · **Size M**

---

## Wave 1 — the core loop at scale

### LP-06 — Job system
Fiber- or job-based scheduling with counters as the only synchronization
primitive; jobs that can yield mid-execution so gameplay code with deep call
stacks can be jobified; "everything is a job" except I/O. Naughty Dog: 6
core-locked workers, 160 fibers, ~800–1000 jobs/frame. Bungie runs almost the
whole engine as a job graph. *[4][5][6][15]* · **Size L** ·
Unlocks everything that currently serializes: quadkey tick, physics, Lua, meshing.

### LP-07 — Data access validation
A resource-tracking system that verifies at runtime that every data access
(reads, writes, creations, destructions) is valid and occurs at a safe point in
the frame. Destiny built exactly this alongside its job graph. WWW already has
authority handover and persistent entity streaming — the multi-writer lifetime
problems this system exists to catch. *[6]* · **Size M** · Prerequisite for
LP-06 at scale.

### LP-08 — SIMD, cache-conscious layout, and memory ordering
SIMD paths for physics stepping, ECS iteration, and encode/decode. Acton's Code
Clinic measures a >20x difference from access pattern alone (3 MB/frame vs
34.7 MB/frame) and names ghost reads/writes and loop-invariant hoisting as the
fixes. On top of that: memory-ordering discipline — every CPU implements
ordering optimistically, so the win is in *contending less*, not contending
faster. *[8][19][263]* · **Size M** · Rapier ships SIMD always-on with
a scalar fallback *[135]*; Jolt added RISC-V RVV *[134]*.

### LP-09 — GPU memory management
Explicit suballocation (256 MiB blocks), placed resources instead of committed,
aliasing of transient resources. AMD's GDC tutorial treats this as required
practice for explicit APIs; Snowdrop's DX12 streamer allocates ~5.9 GB per
1080p frame and manages it by resource type with a residency manager.
*[20][45]* · **Size M** · Prerequisite for LP-13 and the render graph.

### LP-10 — Geometry optimization pass
Vertex-cache optimization (LRU-scored greedy triangle ordering plus a
vertex-buffer reorder for the pre-transform cache), overdraw reduction, and
meshlet generation. Triangle and vertex order directly drive vertex-shader
invocations and bandwidth — Tom Forsyth's linear-speed algorithm is the
reference, and zeux's hardware-counter study shows the ranking is
GPU-dependent, so measure rather than assume. *[322][318]* · **Size M** ·
Prerequisite for LP-17.

---

## Wave 2 — rendering scale

### LP-11 — Bindless resource binding
One global descriptor set with `bindless_srv`/`bindless_uav` indices instead of
a fixed 4-SRV table. Our Machinery measured a 6x command-buffer-building
speedup. Cyberpunk uses bindless multilayer texturing to cut IO pressure.
*[27][88][44]* · **Size M** · Prerequisite for LP-12 and LP-17.

### LP-12 — Material system
Authoring and parameterization: material assets, per-material parameter
binding, shader specialization at import rather than runtime permutation. The
variation mechanism matters as much as the authoring: Our Machinery models
systems as variation dependencies with a hash/bitmask lookup and materials as
arrays of layer-tagged shaders. *[116][119][124][414]* · **Size L** ·
Prerequisite for LP-17 and LP-20.

### LP-13 — Texture streaming and GPU-compressed textures
KTX2/Basis Universal with transcoding to BC7/ASTC/ETC2 chosen from device
features at runtime, mip streaming on a background thread, fractional LOD
clamping. Without it VRAM and load times blow up. *[117][118][126][128][129][104]*
· **Size L** · Pairs with LP-09.

### LP-14 — Occlusion culling
HZB-based culling per cluster, or software occlusion culling for the fallback.
*Life After* cuts draw calls 65% on an iPhone 6s in ~1.5 ms/frame with no false
occlusions. *[32][42][101]* · **Size M**

### LP-15 — LOD and HLOD
Automated mesh analysis for distant LODs (dual meshes, curvature estimation)
rather than hand-authored chains; hierarchical LOD for clustered distant
geometry. Spider-Man 2 automated it when the city doubled and manual LODs
stopped being viable; Virtuos adds geomorphing for crack-free transitions.
*[49][28]* · **Size L**

### LP-16 — GPU-driven rendering
Culling and LOD selection on the GPU: compute shaders, indirect draws, GPU-side
sorting. Every object currently costs a CPU draw call, so a world with millions
of objects cannot scale. Bevy 0.16 and O3DE GeometryView both moved this to the
GPU; Trials Rising held 60 FPS on four platforms this way. *[112][116][24][41]*
· **Size XL** · Depends on LP-06, LP-11, LP-14.

### LP-17 — Mesh shaders and virtualized geometry
Mesh shaders replace the VS/HS/DS/GS chain (`DispatchMesh`, up to 256 verts and
256 tris per meshlet, meshlet compression 100 MiB → 41 MiB). Nanite-style
clusters: 128-triangle groups, a DAG hierarchy, a view-dependent cut, a
visibility buffer of `depth:InstanceID:TriangleID`, and on-demand streaming.
Epic rendered the Moana dataset (146M triangles, 27M instances) at 40+ fps on
an RTX 3080. *[39][42][29]* · **Size XL** · Depends on LP-16, LP-10.

### LP-18 — Temporal upscaling
Jitter (Halton(2,3)), reconstruct previous depth, depth-clip, lock, accumulate,
RCAS. FSR 2.2 is the baseline; Cyberpunk runs FSR 2 in-frame. This is the main
lever for 4K/60. *[40][44]* · **Size M**

### LP-19 — GPU particles
Graph-authored, GPU-simulated, shader-generated particles with a data-driven
workflow; surface- and shader-driven emission. Frostbite's Emitter Graph is the
reference shape. *[25][26][213]* · **Size L**

### LP-20 — Real-time global illumination
Ray-traced multi-bounce diffuse GI (RTXGI) or a probe field computed from a
voxel/triangle scene representation (Enlisted). *[31][30]* · **Size L**

### LP-21 — Shader build dependency tracking
Include-dependency metadata per shader, incremental compilation, hot reload of
shaders without a restart, and an embedded shader dump for shipped builds.
Wicked's `.wishadermeta` is the concrete pattern. *[300]* · **Size M** ·
Prerequisite for LP-38.

---

## Wave 3 — a character that works

### LP-22 — Animation blending, state machines, and IK
Blend trees, layers, a state machine, and IK. WWW imports rigs and identity but
cannot blend or transition, which blocks any gametype needing more than one
clip — locomotion, hit reactions, aiming, traversal. Watch_Dogs' MASK stack is
a multi-layered replicated state machine over dynamic animation trees; GDC
devoted a full day to IK math. *[71][72][81][82]* · **Size L**

### LP-23 — Animation retargeting and humanoid bone mapping
Map arbitrary source rigs onto the engine's humanoid skeleton, handling
world-space vs local-space differences and inverse bind matrices, offline and at
runtime. VRM and Mixamo rigs do not agree; without retargeting every avatar
needs hand-authored clips. *[298]* · **Size M**

### LP-24 — Navmesh and pathfinding
Recast-class navmesh generation, pathfinding, and crowd steering. Godot ships
a dedicated navigation server; WickedEngine ships 3D path finding. *[130][124]*
· **Size M**

### LP-25 — Audio engine
In-house, bootstrapped from first principles: a software mixer with fixed-N
sample mixing, gain positioning for surround layouts, a spread parameter for
point sources, audio state machines, camera listener math, weak event pointers,
volumetric circle-box tests, and a lock-free triple-buffer to the audio thread.
*[60][61][255]* · **Size L**

### LP-26 — Spatial audio
HRTF binaural rendering, occlusion and transmission, real-time physics-based
reverb. Steam Audio shipped all three in *Budget Cuts* as gameplay-critical
(locating enemies by sound). *[63][62]* · **Size M**

### LP-27 — Animation-driven locomotion
Motion correction that preserves animation content while staying responsive —
the tension Just Cause 3's talk is entirely about. Partly blocked on LP-22.
*[79][71]* · **Size M**

---

## Wave 4 — multiplayer that holds up

### LP-28 — Delta compression
Per-entity, per-command-frame deltas: changed instances, changed variables with
array index ranges, changed state/action indices, changed owner variables.
Deltas retained until all clients ack; packets built as the union of deltas
over a frame range (range start 0 = full update). Cheapest large win in the
whole list: bandwidth scales with changed fields, not entity count.
*[56]* · **Size M** · Prerequisite for LP-29.

### LP-29 — Snapshot interpolation
Render remote entities in the past and interpolate between snapshots, with a
fixed delay as the price of smoothness. AOI decides *what* to send; this decides
*when* to render it. Without it remote avatars judder under jitter and loss.
*[59]* · **Size S**

### LP-30 — Lag compensation
Server-side rewind/re-simulate within a bounded window (50 ms in one
shipped-style implementation), plus client-side anti-lag. Required for any
hitscan gametype; without it high-ping players are systematically
disadvantaged. *[56][78]* · **Size M**

### LP-31 — Rollback netcode
Save-state / re-predict: restore to the last non-predicted frame, advance with
newly received input, re-predict the intervening frames, render. GGPO is the
canonical implementation; NetherRealm rolls back up to 8 frames inside 16 ms.
Quadkey AOI cannot help — the problem is local-entity simulation.
*[57][58][59][66][148]* · **Size L** · Depends on LP-03, LP-04, LP-28.

### LP-32 — Network debugging and profiling tooling
A network profiler, artificial latency and packet loss, and an in-editor
multi-node view. Our Machinery's network frontier (nodes, pipes, packet types,
guaranteed delivery, component replication, gamestate sync) is the reference
architecture; the tooling is what makes it debuggable. *[253][308]* · **Size M**

### LP-33 — Server meshing control plane
A cross-server player-migration service and an autoscaling/ops story on top of
the quadkey shard boundary. Ragnarok Online 3 uses a centralized AvatarService
for seamless state continuity during dynamic migration plus Kubernetes
autoscaling for million-CCU targets. *[74][73]* · **Size L**

### LP-34 — Matchmaking and parties
Skill-based matchmaking, parties and lobbies, and a session directory. WWW has
instance matchmaking but no player grouping or skill rating. TrueSkill (and
TrueSkill through time) is the standard rating model; Apex Legends covers the
evolution of a production matchmaker. *[195]* · **Size L**

### LP-35 — Anti-cheat and client attestation
Server authority stops state tampering, not wallhacks or automation. Client
attestation, integrity checks, and a clear statement of what the server will
not trust. *[413]* · **Size L** · Note: this is about fair play between
players, not platform security — see the accepted-risk section.

---

## Wave 5 — creators can build

### LP-36 — Asset pipeline as a graph
A re-riggable, hot-reloadable import graph with an immutable link hierarchy, so
re-import is a graph rebuild rather than a full cook. Our Machinery's Creation
Graphs are the reference. Also: add a **build key** (importer version + option
set) to the DDC cache key — today the key is source content only, so an importer
change serves stale blobs. *[84][86]* · **Size L**

### LP-37 — Import-time shader processing
Generate shaders at import, serialize to binary, and stop parsing at runtime.
Aras's argument: fixed-function shaders belong at import, and shader labs
should be binary. *[96][94]* · **Size M**

### LP-38 — Shader graphs and material editor
Node-based authoring over LP-12, with permutation management. Unity's Shader
Graph is production-proven; Bungie's Destiny shader pipeline is the AAA
counterpart. *[374][360]* · **Size L**

### LP-39 — Visual scripting for designers
Node graphs layered over the Lua, so designers can build without writing code.
Three production references: God of War Ragnarok's visual scripting, DECIMA's
visual programming for every discipline, and ArenaNet's model of simple
building blocks with infinite composability. *[302]* · **Size L**

### LP-40 — Cinematics and sequencing
A timeline/sequencer: camera tracks, animation sequencing, event tracks,
in-engine cutscenes. Frostbite's cinematics tools write-up is the honest
account of what it takes; StarCraft II and Love and Deepspace cover the
pipelines. *[370][None][198]* · **Size L**

### LP-41 — VFX authoring
Node-based VFX authoring over LP-19, with simulation caches. Niagara is the
reference; Bungie's Destiny VFX talk covers the production side. *[213][369]*
· **Size L**

### LP-42 — UI system
A real UI stack: retained or immediate mode, a widget tree, layout and
containers, a focus/responder chain with keyboard navigation, data binding, and
text rendering. Two hard sub-problems: **text shaping** (harfbuzz-equivalent —
without it non-Latin scripts render wrong and all in-engine text is blocked)
and **one-draw-call rendering** (UIPainter's tile-based SDF approach).
*[181][196][404][290][257][314]* · **Size XL**

### LP-43 — Terrain system
Heightfield or voxel storage, CDLOD/geomipmap LOD, splatmaps, and procedural
generation with erosion. The quadkey tile store can already hold terrain; there
is no authoring or LOD layer over it. Far Cry 5, Ghost Recon Wildlands and
Call of Duty's terrain talks are the references. *[202][368][383]* ·
**Size XL**

### LP-44 — DCC tools pipeline
Importers, exporters, and a tools layer that connects digital content creation
apps to the engine. Guerrilla's Horizon tools pipeline is the reference for
what a production tools team builds. *[335]* · **Size L**

### LP-45 — Designer-facing data tooling
Editors and validators over `mode.json`, the config schema, and the Lua — so a
designer can author and check a gametype without reading C. *[302]* · **Size L**

### LP-46 — Build, cook, and deploy pipeline
Asset cooking, chunking, patch/delta generation, and CDN deployment. DDC and a
manifest exist; release deployment does not. *[192][193]* · **Size L**

### LP-47 — Engine testing and CI
Deterministic replay as a regression tool, automated test frameworks, fuzzing,
soak testing, and performance-regression tracking. TestMonkey (Santa Monica),
Activision's automated testing and MergeBot, and Retro City Rampage's replay
testing are the references. 6121 assertions exist; replay-based regression does
not. *[284][283]* · **Size L**

### LP-48 — Large-team source-control workflow
Branching, merging and asset workflow at team scale. *[402]* · **Size M**

### LP-49 — Platform and certification engineering
Console TRC/XR, submission, patching, age ratings, store compliance.
Certification requirements are testable and enumerable. *[191][393]* ·
**Size M**

---

## Wave 6 — the platform

### LP-50 — Persistence layer
A durable store with an append-only event log, projections, and a
snapshot-plus-tail save format. Streaming is transport, not durability: without
snapshots and tail replay, a server crash loses world state. Star Citizen's
Replication layer persists every network-replicated entity into a graph
database. *[141][153]* · **Size XL**

### LP-51 — Identity and social graph
Cross-world identity, accounts, friends, presence, blocks, and trust ranks. AOI
is spatial, not social. Identity is already half-built in `wb_avatar_identity`
(JSON records); the social graph is not. *[173][167]* · **Size L**

### LP-52 — Discovery and curation
How players find gametypes: a catalog, search, charts, tags, featured slots,
and social-graph signals. Roblox runs two-stage retrieval→ranking on 28-day
retention signals — the ranking objective is the product decision.
*[164][165]* · **Size L**

### LP-53 — Content moderation and safety
A policy layer, distinct from the Lua sandbox (which is a security boundary,
not a policy one): text and voice filtering, reporting queues, human review,
appeals, and real-time feedback. Roblox's AI moderation at scale and VRChat's
safety and trust system are the references. *[166][167]* · **Size XL**

### LP-54 — Age gating and the legal posture
Maturity labels, age verification, regional content policies, terms of service,
and a Section 230 stance. COPPA and the FTC's 2026 age-verification policy
statement make parts of this a compliance requirement, not a choice.
*[179][239][175][176]* · **Size L**

### LP-55 — Creator economy and publishing
Publishing pipelines with version history, staged rollout, publish notes and
one-click restore to a known-good version; then currency, revenue share,
cashout, upload fees, and an asset marketplace. Fortnite pays 40% of net shop
revenue by engagement. *[169][221]* · **Size XL**

### LP-56 — Interoperability and federation
OMI glTF extensions, Metaverse Standards Forum participation, and portable
avatar/inventory formats. WWW consumes glTF/VRM but emits no extensions of its
own. *[172][218]* · **Size XL**

### LP-57 — AI-generated content and validation
LLM/GenAI pipelines for world and content creation, with validation gates:
generated content must be checked against a world model before publication
(QuestGuard's model — schema contract, deterministic validators, bounded repair
loop). Runtime model output is logged as an event with an `Agent`/`LLM` origin
and replayed from the log, never re-executed. *[157][158][153]* · **Size XL**

### LP-58 — Platform live ops
Remote config, feature flags, content updates without patching, A/B testing,
event scheduling, and platform-level telemetry. *[None][220][403]* ·
**Size L**

### LP-59 — Accessibility and localization
Colourblind modes, input remapping, subtitle systems, text scaling, reduced
motion, screen-reader hooks; and an i18n pipeline with RTL and voice-over.
Platform certification increasingly requires these. *[398][204][339]*
· **Size L**

---

## Best source per pad (third-degree pass)

Each existing pad pinned to the single best authoritative source (talk+speaker+year, canonical doc, or reference project). Full reasoning and citations live in the research reports `11..15-*.md`; the ledger holds every URL.

**RENDER**
- LP-09 GPU memory — [532] *Practical DirectX 12* (GDC 2016)
- LP-11 bindless — [88] Our Machinery, *Moving The Machinery to Bindless*
- LP-12 materials — [401] *Nanite GPU-Driven Materials* (GDC 2024)
- LP-13 texture streaming — [532] *Practical DirectX 12* (residency)
- LP-14 occlusion culling — [522] *Masked Software Occlusion Culling* (Intel)
- LP-15 LOD/HLOD — [523] UIUC CS418 *Nanite: Streaming, LOD, Occlusion*
- LP-16 GPU-driven — [42] *Nanite* SIGGRAPH 2021 Advances (Karis/Stubbe/Wihlidal)
- LP-17 mesh shaders / visibility buffer — [401] *Nanite GPU-Driven Materials*
- LP-18 temporal upscaling — [40] *Temporal Upscaling Past, Present, Future* (GDC 2023)
- LP-19 GPU particles — [208] *Beyond Emitters* (GDC)
- LP-20 real-time GI — [531] *Lumen in Fortnite Ch.4*; [529] *Lumen Technical Details*
- LP-21 shader deps — enhanced-barrier work [524][527]

**ANIMATE + CONNECT**
- LP-22 blending/ASM/IK — [71] Therriault, *The Character of Watch_Dogs* (GDC 2015); IK [72]
- LP-23 retargeting — [504] Gleicher, *Retargetting Motion to New Characters* (SIGGRAPH 1998)
- LP-24 navmesh — [502][503] Recast/Detour (Mononen)
- LP-27 locomotion — [500] Clavet, *Motion Matching* (GDC 2016)
- LP-28 delta compression — [512] Fiedler, *Snapshot Compression*
- LP-29 snapshot interpolation — [505] Fiedler, *Snapshot Interpolation*
- LP-30 lag compensation — [506] Bernier, *Latency Compensating Methods* (GDC 2001)
- LP-31 rollback — [148] GGPO Developer Guide; [66] *8 Frames in 16ms* (GDC 2015)
- LP-32 net debugging — [509] Unreal *Networking Insights*
- LP-33 meshing control plane — [508] Cloud Imperium, *Server Meshing Q&A*
- LP-34 matchmaking — [510] *TrueSkill* (NIPS 2006)
- LP-35 anti-cheat — [425] GameSafe iOS attestation; [516] Riot *Fog of War*

**AUTHOR**
- LP-36 asset graph — [481] Unity, *The new Asset Import Pipeline* (2019)
- LP-37 import-time shaders — [96] Aras, *Unity's Shader Pipeline* (2013)
- LP-38 shader graphs — [232] Unreal *Material Editor*; [274] *Shader Permutations*
- LP-39 visual scripting — [233] Unreal *Blueprint Overview*
- LP-40 cinematics — [482] Unreal *Cinematics and Movie Making*
- LP-41 VFX — [213] *Niagara*; [208] *Beyond Emitters*
- LP-42 UI system — [189] Godot *UI*; [198] *UIPainter* (GDC 2024)
- LP-43 terrain — [202][289] *Terrain Rendering in Far Cry 5* (GDC)
- LP-44 DCC tools — [485] *Project Skyline* (GDC 2011); [484] Unreal *Live Link*
- LP-45 designer data tooling — [486] Unreal *Data Driven Gameplay Elements*
- LP-49 certification — see the platform pass (10-meta-platform.md)

**HOST / PLATFORM**
- LP-46 build/cook/deploy — [468] Agones *Best Practices*
- LP-50 persistence — [478] Epic Online Services *Player & Game Data*
- LP-51 identity/social — [478] EOS *Overview*
- LP-52 discovery — Roblox *Optimizing Discovery* (retained)
- LP-53 moderation — Roblox *AI to Moderate Content* (retained)
- LP-54 age gating/legal — [472] AEPD, *Data protection in video games* (2026)
- LP-55 creator economy — [477] Roblox, *Vision for the Roblox Economy*
- LP-56 interoperability — OMI glTF / MSF (retained)
- LP-57 AI content — Roblox *Games with Generative AI* (retained)
- LP-58 live ops — [475] PlayFab *data & analytics*
- LP-59 accessibility — Microsoft GDK a11y (retained)

**INSTRUMENT + SIMULATE**
- LP-01 profiling/telemetry — [147] Tracy Profiler; [567] *Unified Telemetry* (GDC 2016)
- LP-02 memory architecture — [18] Acton, *Data-Oriented Design and C++* (CppCon 2014); [13] MacDougall low-fragmentation
- LP-03 determinism — [54] Frontier, *Cross-Platform Determinism* (GDC 2024); [541] Fiedler *Deterministic Lockstep*; [542] Yates fixed-point
- LP-04 fixed timestep — [540] Fiedler, *Fix Your Timestep!*
- LP-06 job systems — [5] Gyrling, *Parallelizing the Naughty Dog Engine* (GDC 2015); [552] Our Machinery fiber job system
- LP-07 data-access validation — [6] Genova, *Multithreading the Entire Destiny Engine* (GDC 2015); [18] Acton DOD
- LP-08 SIMD / memory ordering — [554] Preshing, *Acquire and Release Semantics*; [548] Drepper, *What Every Programmer Should Know About Memory*
- LP-10 geometry optimization — [322] Forsyth, *Linear-Speed Vertex Cache Optimisation*; [318] zeux, *Learning from data*

**NETWORKING (deep)**
- QUIC / WebTransport — [579] RFC 9000; [614] RFC 9221 (unreliable DATAGRAM); [578] MDN WebTransport
- Fragmentation / MTU / PSN — [577] Fiedler, *Packet Fragmentation and Reassembly*
- NAT traversal — [613] RFC 8445 (ICE / STUN / TURN)
- Interest-management variants — [580] VELVET (de Oliveira); [629] Fiedler priority accumulator
- Server meshing / authority transfer — [623] SpatialOS (via SparkEngine); [624] HeliosEngine epoch-fenced handoff
- Netcode telemetry & replay — [604][626] Aldridge, *I Shot You First* (GDC 2011)
- Lag-comp / rollback — [603] *Formalizing Rollback Netcodes* (OPODIS 2025)
- Dedicated-server orchestration — [606][631] Agones docs + FAQ
- Edge / anycast — [607] Edgegap orchestration
- QoS / traffic shaping — [611] NetEm; [608] WireGuard DSCP
- Encryption / anti-tamper — [608] WireGuard protocol; [609] 6it.dev
- Clock sync — [610] NTP; [612] Rivas Acosta
- Delta baseline / ack — [581] Valve *Source Multiplayer Networking*
- Prediction / reconciliation — [67] Gambetta; [1] Overwatch
- Bandwidth budgets — [604] Aldridge; [581] Source `rate`

**NESTING (universe → stadium)**
- universe — [587] Roblox *publish games and places*; [630] Universes API
- world — [591] Minecraft *World*
- continent — [589] Second Life *Geography* (grid / continent / sim)
- city — [599] Second Life *Land* (region/sim, 100-avatar cap)
- patch — [593] Bing *Maps Tile System* (quadkey); [594] S2; [595] H3
- stadium — [597] Roblox *client-server runtime*; [600] TeleportService

**GEO + RESERVATION**
- reservation — [574] Agones *GameServerAllocation*
- free geo stack — [576] CesiumGS *3D Tiles* (OGC); [582] OGC CDB; [583] OSM ODbL; [616] MS Building Footprints; [622] USGS Mars MOLA DEM
- planetary nesting — [573][621] OGC DGGS (Topic 21); [618] OGC AI-DGGS Pilot

---

## Wave 7 — third-degree additions

New gaps the first two passes never named, each grounded in a primary source. Same dependency logic: a pad is one crossable deliverable with a gate.

### RENDER

### LP-60 — GPU synchronization & resource-state/barrier management
The single hardest explicit-API concern, unnamed until now: implicit promotion/decay, layout transitions, cache flushes, aliasing. Transition-to-COMMON is always a pipeline stall; enhanced barriers untangle work-sync / layout / cache-flush into independent types and drop implicit promotion. Godot's render-DAG + topological sort is the existence proof for automatic barrier insertion. WWW's single-queue, fixed-4-SRV backend inherits this the moment it adds aliasing, residency, async compute, or streaming. *[525][527][524][518]* · **Size L** · Prerequisite for LP-09/LP-16 residency and async.

### LP-61 — HDR / color management / tone mapping
The pipeline has no working color space. scRGB as the composition space, PQ/ST.2084, app-side tone mapping with MaxCLL; import decode to linear, light/composite scene-linear, then tone-map and re-encode. Cheap to decide now, painful to retrofit once bloom/fog/SSR land. *[521]* · **Size M**

### LP-62 — Virtual texturing as a residency/feedback system
Beyond per-mip streaming: one virtual address space, a page table, a feedback pass producing per-pixel page+mip requests, a CPU page manager (LRU + pinned low-res), coarse-mip-first fill. A natural fit for WWW's quadkey streaming tile store — Megatexture was exactly virtual texels over a big world. *[526][520][537]* · **Size L**

### LP-63 — Reflection-tier design (SSR → RT → hybrid)
No reflection path exists. DOOM Eternal puts SSR in the forward pass (single ray, ignore BRDF, TAA denoise); Lumen traces glossy rays at half-res and falls back to SSR for mirrors. Plan a tier (screen-visible SSR, off-screen RT fill), don't bolt on one technique. *[536][531][529]* · **Size M**

### LP-64 — Multithreaded draw submission
The render graph is recorded single-threaded. Task-graph: one render thread submits, workers build command lists; 15–30 CLs / 5–10 `ExecuteCommandLists` per frame; ≥200–500 µs GPU work per submit; PSOs compiled on workers; small root-signature table. Scales everything in Wave 2. *[532][534][518]* · **Size L**

### LP-65 — Multi-GPU / multi-adapter RHI awareness
No multi-GPU story. D3D12 nodes, cross-adapter heaps/fences, node masks; cross-adapter memory on the primary adapter; PCIe bandwidth caps 4K-HDR transfer. Design an abstraction (second adapter for async compute / display), not a split-screen code path. *[528][532]* · **Size L**

### ANIMATE + CONNECT

### LP-66 — Animation compression (ACL)
WWW stores full-resolution glTF clips verbatim. ACL (Nicholas Frechette) is the reference: bit-aligned values instead of naturally aligned integers, every sample retained (no key reduction), uniform memory, cache-friendly sampling — decompression speed and memory directly bound how many characters a C11 engine can drive. Compress at import into the DDC; sample the compressed format at runtime. *[498][499][82]* · **Size L**

### LP-67 — Motion matching / data-driven animation
No mechanism to select frames from a clip database at runtime. Motion matching (Clavet, GDC 2016) continuously searches a mocap database for the frame matching current pose + desired future trajectory and blends to it, eliminating hand-authored transitions. For WWW: a pose/trajectory feature vector, a search (kd-tree / PCA-whitened features), inertialized blending. Feeds from LP-66. *[500]* · **Size L**

### LP-68 — Crowd / SIMD / GPU-batched skinning
One skeleton per character, no batch path — large crowds (the engine's whole demo) are unreachable. GPU Gems 3 ch.2 bakes all animation frames into a texture read in the vertex shader with `SV_InstanceID` (~10k characters at 30 fps on 2007 hardware); the CPU tier is SIMD batch skinning; DetourCrowd is the steering companion. *[501][502]* · **Size L**

### LP-69 — Physics-blended & procedural animation
No ragdoll/blend-to-physics path or secondary motion. State of the art is Clavet's ragdoll motion matching (GDC 2022); the near-term subset is active-ragdoll blend-in/out (a 0.1–0.3 s physics-weight ramp to avoid the pop). Needs a physics pose readback + blend-weight channel in the pose stage. *[514]* · **Size M**

### LP-70 — Transport & reliability layer (reliable-UDP, encryption)
Raw sockets, no reliability/sequencing. The three-message-class model: unreliable/latest-wins (movement), reliable/ordered (events), reliable-by-redundancy (inputs). Compare ENet, KCP, Valve GameNetworkingSockets (Curve25519 + AES-GCM-256, NAT traversal), QUIC, WebTransport. The encrypted channel also serves LP-35. *[507]* · **Size L**

### LP-71 — Interest management beyond the quadkey (variable AOI, priority, visibility)
The quadkey is a fixed-geometry culler; the literature names its failure mode — fixed AOI breaks under player clustering ("flash mobs"). Fix: variable-radius AOI (K-nearest) via the spatial store, priority-based relevancy (a per-object priority accumulator), and information-exposure control (withhold what a client cannot see — the anti-wallhack boundary). *[511][507][516]* · **Size L**

### LP-72 — Headless dedicated-server simulation & netcode QoS telemetry
The mesh is the partitioning story; nothing runs authoritative simulation headless at scale with the telemetry to operate it. Measure tick-duration p99, per-tick entity counts, packet queues, bandwidth-per-player; network-emulation + replay tooling. The netcode analogue of LP-01; LP-33 autoscaling and LP-32 tooling share this substrate. *[517][515][509]* · **Size L**

### AUTHOR

### LP-73 — Sandboxed hot-reload reliability / live-edit loop
Hot-reload has no guarantees: no syntax gate before swap, no `on_reload` lifecycle, no defined boundary between reloadable (code, UI) and non-reloadable (physics bodies, sockets, engine state). Defold's hot-reload manual is the discipline reference; a reliable live-edit loop is a hard requirement for a gametype-first engine. *[488]* · **Size M**

### LP-74 — Editor undo / transaction / command system
Operation replay exists but no transaction system: no command objects with inverse ops, no memento for non-invertible ops, no history bound, no idempotency. Command pattern + memento, baked in now; the production pattern for multi-step/agent edits is a plan + effect journal with compensating actions and idempotency keys. *[491][489]* · **Size L**

### LP-75 — Importer-versioned DAG asset builds + build-key correctness
The DDC key is source content only, so an importer change serves stale blobs. Every transform is a pure node versioned by a function hash; build key = hash(source, settings, importer version); incremental DAG rebuild; determinism (same bytes from every toolchain, or a toolchain term in the key). *[481][490]* · **Size L**

### LP-76 — Cloud / dedicated build farms
The asset pipeline is single-process; no way to parallelize C++/shader compile or cooking across machines. Reference: Horde + Unreal Build Accelerator (agent pool, artifact store, remote execution, auto-scaling). A creator's cook of a large world must not block their machine. *[487]* · **Size L**

### LP-77 — Multi-user co-authoring / co-located editing
The editor is single-user. Reference: Unreal Multi-User Editing — client-server transaction bus, instant level sync, asset sync on save with hot-reload, presence, optimistic locking, undo/redo integrated with source control. The world-data model must be transaction-granular. *[483]* · **Size XL**

### LP-78 — Asset validation / lint pipeline
glTF/VRM import has no validator gate or lint rules. Rules-based validation with machine-readable diagnostics (asset id, rule, severity, suggested fix) consumed identically by editors and agents; geometry/material/rig checks committed as tests. Khronos' glTF validator is the binary gate; `assetpipe` shows the standalone form. *[490][492]* · **Size L**

### LP-79 — Asset provenance & licensing
No record of where an asset came from, who holds rights, or which license governs it — a real liability when creators and AI tools both inject content. Provenance+license as a required field; the build refuses unlicensed assets; an SPDX allowlist gate; automated credits; track model-id/prompt-hash/seed so a generated asset can be audited later. *[490][492][493]* · **Size L**

### HOST / PLATFORM

### LP-80 — Multi-tenant tenancy & per-gametype isolation
Many gametypes co-host on a shared fleet with no tenancy model: no per-gametype control-plane isolation, no resource quotas, no blast-radius containment. Reference: vCluster + KAI Scheduler per-team control planes with no lateral path; Agones Pod Security Standards; Red Hat's isolation stack (MIG vs time-slice). *[467][468][497]* · **Size L**

### LP-81 — Service-to-service security / rate limiting / abuse defense
Platform APIs (matchmaking, discovery, economy, moderation) have no request-defense layer. Reference: Cloudflare Rate Limiting — edge blocking before origin, limits keyed on headers/session, botnet neutralization, login brute-force protection; Cloudflare for Gaming adds DDoS/WAF/API security. *[470]* · **Size L**

### LP-82 — Offline / edge caching & global distribution
Nothing tiers content to the edge; every asset request hits origin. Reference: Cloudflare Tiered Cache (lower + upper tiers, origin reaped on miss, topology choices). Needed for gametype bundles, UI assets, and glTF/VRM content. *[471]* · **Size M**

### LP-83 — Data portability / GDPR / privacy posture (export + erasure)
The platform holds identity, social graph, inventory, telemetry with no export/erasure/portability. AEPD's June 2026 guidance: Art. 20 portability ("export my data" buttons with previews), short raw-log retention, processor coordination; EDPB requires simple, accessible, free erasure. Non-compliant by default under EU law. *[472][495][475]* · **Size L**

### LP-84 — Availability / SLO engineering & on-call for the platform itself
No SLI/SLO, error budget, burn-rate alerting, or on-call for the host layer. Google SRE: error budget = 1 − SLO; multi-window multi-burn-rate alerts; budget policy halts changes on exhaustion and mandates postmortems. Per-service SLOs for discovery/identity/economy/asset-delivery. *[473][496]* · **Size L**

### LP-85 — Real-money economies & in-game purchase compliance
The backlog names creator payout but not the consumer-spend side. Regulators are escalating (NY AG loot-box suit, FTC dark-pattern positions, Belgium/Germany/EU rules, EU Digital Fairness Act). Ship consent/restriction, probability/loot-box disclosure, and regional gating or inherit gambling-law liability. *[480][494][472]* · **Size L**

### LP-86 — Runtime plugin/module & avatar-item marketplace
Discovery covers gametypes, but users can't obtain engine plugins, avatar items, or reusable content in a trusted, versioned way. Reference: Godot Asset Store (reviews, versions, changelogs, publisher analytics); Roblox avatar-items-as-content. Distinct from gametype discovery. *[476][477]* · **Size XL**

### INSTRUMENT + SIMULATE

### LP-87 — Desync prevention & triage tooling
LP-03 detects divergence; nothing localizes it. Record-replay-compare on every build/platform, state dumps + binary diffs at the first divergent tick, headless bot fuzzing that saves the failing seed, lockstep frame-steppers. Factorio CRCs the map every tick and diffs replays; Frontier built desync-analysis tooling; Riot logs hierarchical state + a replay farm. *[562][566][54]* · **Size L**

### LP-88 — Causal profiling (what to optimize, not where time is)
Coz (Curtsinger & Berger, SOSP 2015) virtually speeds up a line by slowing concurrent code and reports the predicted speedup — a 55%-runtime function may yield 4.5% if optimized away. Turns LP-01's counters into "which scope actually moves frame time". *[545][571]* · **Size M**

### LP-89 — End-to-end latency measurement (input-to-photon)
Frame time (LP-01) and network (LP-32) don't measure felt latency. NVIDIA PCL decomposes input-to-frame-start / frame-start-to-present / present-to-displayed and emits ETW markers (SIMULATION_START, RENDERSUBMIT_START, PRESENT_START/END) for per-frame correlation; Reflex/Frame Warp act on them. *[557][556]* · **Size M**

### LP-90 — Statistical benchmarking methodology
Budgets (LP-05) need trustworthy numbers. Google Benchmark (DoNotOptimize/ClobberMemory, repetitions, median/stdev, interleaving, warmup) over hand-rolled chrono; treat microbenchmarks as relative and bias-prone (DCE, cache state, DVFS, link order). Arm's GPU rules: frame-cost-matched workloads, 15 s runs with 5 s discarded. *[559][564][565]* · **Size M**

### LP-91 — Compiler/toolchain determinism flags & static audits
The toolchain silently breaks LP-03: enforce `-ffp-contract=off` / `/fp:precise` (no fast-math, no FP contraction), and static-scan for unseeded RNGs, pointer-keyed iteration, uninitialized memory. salivity's cross-platform determinism guide is the clearest statement of FMA / x87-vs-SSE / transcendental divergence. A build-config contract, not a style preference. *[562][151]* · **Size M**

### LP-92 — Memory measurement practice (alloc tail latency, per-tag budgets, steady-state rate)
A generic malloc has a heavy tail (~29 ns median, ~115 µs p99 for 2,000 allocs — 20 slow calls cost more than 1,980 fast ones); watch p99 allocation latency, not the mean. Measure per-frame allocation counts (steady state → zero) and per-tag bytes against budget; treat a p99 spike as a frame-budget event. Naughty Dog's tagged heap is the design response. *[563][5][13]* · **Size M**

---

## Wave 8 — focused additions (networking · nesting · geo)

### NETWORKING

### LP-93 — QUIC / WebTransport transport + connection migration + browser path
No QUIC option, no 0-RTT reconnect, no NAT-rebinding / Wi-Fi↔cellular migration, no browser API. RFC 9000 is the canonical transport; RFC 9221 adds the unreliable DATAGRAM extension explicitly motivated by gaming; WebTransport is the browser path. *[579][614][578]* · **Size L**

### LP-94 — Packet fragmentation / reassembly + MTU/PSN discipline
No 1200-byte MTU budget, no fragment/reassembly layer, no PSN wrap handling; a burst delta snapshot can exceed MTU and drop the whole packet. Fiedler: 1472 upper / 1200 conservative MTU, 16-bit PSN, free fragment packet type, loss-amplification 1−p^n. *[577]* · **Size M**

### LP-95 — NAT traversal (ICE/STUN/TURN) + signaling + relay fallback
No STUN reflexive discovery, no TURN relay for symmetric NATs, no ICE candidate exchange or connect-token gateway. RFC 8445. *[613]* · **Size L**

### LP-96 — Clock synchronization / time authority
No client-server clock sync, no RTT-based offset estimation, no drift correction — lag comp, rollback and replays lack a shared time base. NTP's symmetric-delay offset estimation; Rivas Acosta's game-server adaptation. *[610][612]* · **Size M**

### LP-97 — QoS / traffic shaping + per-client bandwidth budgets
No `rate`-style per-client cap, no DSCP marking, no netem-style network emulation in the test suite. NetEm for shaping; WireGuard's DSCP discipline; Source's `rate` console variable for the cap. *[611][608][581]* · **Size M**

### LP-98 — Edge / anycast server placement + latency-based orchestration
No ideal-server placement per match, no regionless edge caching; server placement is treated as hosting, not a netcode lever. Edgegap: placement across 615+ edge locations, 58% latency reduction, ~2 s cold start. *[607]* · **Size L**

### LP-99 — Cross-server entity handoff wire protocol
No freeze/HANDOFF/ACCEPT/ROUTE/epoch-fencing/grace-window protocol for authority transfer, so LP-33's meshing control plane has no concrete wire contract. SpatialOS's authority/interest model; HeliosEngine's epoch-fenced handoff. *[623][624]* · **Size L**

### NESTING (universe → stadium)

### LP-100 — Cross-level authority & travel
No model for which level's server owns a traveler at a boundary, and no atomic handover. Roblox teleports by PlaceId (cross-experience travel off by default); Second Life crosses sims by exiting one server and entering another. *[586][589]* · **Size L**

### LP-101 — Level-of-detail across levels
LOD is defined within a level (quadkey length = LOD; S2 31 levels; H3 16 resolutions) but never across levels — no parent-cell imposter/aggregate, no per-level detail budget. *[593][594][595]* · **Size L**

### LP-102 — Per-level persistence
No durability model for which level owns durable state (world seed → continent geometry → city props → patch terrain → stadium session). Minecraft persists per-dimension folders; Roblox DataStore is consistent per game across places/servers. *[591][602]* · **Size L**

### LP-103 — Parent-child interest propagation
Interest is computed at the patch level only; no parent-cell AOI, no aggregation of children's interest, no hierarchical culling. Roblox radius-based instance streaming; WoW area-of-interest/layers. *[588][598]* · **Size L**

### LP-104 — Naming & addressing places across levels
No hierarchical address (universe/world/continent/city/patch/stadium); a place is addressed only by flat quadkey + gametype. Roblox uses /universes/{universe_id}/places/{place_id}; Second Life parcels carry a local id plus global UUID. *[630][599]* · **Size M**

### LP-105 — Instance placement at a level
No rule for which level a stadium binds to, and no dedicated-node vs shared-node policy. Roblox hosts a server per place; WoW hosts instances on a realm; EVE dedicates a node to one solar system. *[597][598][596]* · **Size M**

### LP-106 — Per-level capacity & layering
No level-aware capacity/layering — cannot spawn sibling stadiums for a hot patch or pin a party to a layer. Second Life caps a region at 100 avatars; WoW shards/layers crowded zones to keep parties together. *[599][598]* · **Size L**

### GEO + RESERVATION

### LP-107 — Nested-volume reservation ledger / consensus
No shared atomic ledger for who holds which nested spatial volume; every platform reserves a different unit (server slot, place GUID, compute, parcel) in its own silo. Agones GameServerAllocation specifies the lifecycle (Ready/Allocated/Shutdown), atomic conflict handling, and counters/lists capacity quotas. *[574]* · **Size XL**

### LP-108 — Nested-volume spatial queries
The quadkey answers point-to-cell, not the containment / overlap / adjacency / smallest-enclosing-volume queries needed to reserve nested volumes. *[593][594]* · **Size L**

### LP-109 — LOD-across-planets
No single LOD chain spans universe/planet/continent/city/building/room. DGGS gives hierarchical cells and 3D Tiles a render hierarchy, but nothing ties them across planets. *[573][576]* · **Size XL**

### LP-110 — Open-data provenance & licensing for AGI
ODbL (OSM), CDLA-Permissive (MS Building Footprints) and public-domain planetary DEMs (USGS) have no per-cell provenance/license ledger, and the content-addressed DDC carries no license metadata. Required before AGI-authored worlds ingest open geo data. *[583][616][622]* · **Size L**

### LP-111 — Streaming planetary terrain
The USGS Mars MGS MOLA DEM is a single 2 GB GeoTIFF; WWW has no terrain representation and no planetary terrain streaming format. *[622]* · **Size XL**

### LP-112 — AGI-authored place validation
No gate checks AGI-authored places against the source DEM, reservation/containment consistency, or license completeness before publish. The OGC AI-DGGS Pilot frames DGGS as the authoritative content store for AI/RAG queries — the validation substrate. *[618]* · **Size L**

### LP-113 — Per-volume capacity / quotas
Reservation models have capacity (Agones counters/lists, Second Life prim allowances, AWS AZ capacity) but WWW has no capacity model for nested volumes (stadium/patch/city/planet). *[574][585][584]* · **Size L**

---

## Accepted risk — not a pad

**The mod sandbox is escapable, and that is accepted.** `sandbox_blocked()` is a
14-name denylist and `_G` is not on it, so a "sandboxed" mod can reach the real
globals table — demonstrated by a mod writing a file to disk through
`_G.io.open(...)`. The existing gate only checks plain names, so it passes while
the sandbox is open.

This is **not** being treated as a defect. WuBuOS is an agentic OS and is
insecure by design; AGI must be insecure for human exposure, and the platform
tells users outright not to bank on it. This is Ready Player One hosting and
Second Life danger. The pad that would close it (a capability sandbox plus
resource limits, bytecode rejection and an interrupt watchdog *[136][69]*)
exists as a note, not as work.

---

## What not to break

- **The quadkey spine.** One spatial index for AOI, streaming, and sharding.
  Every pad above reads it; none forks it.
- **The render graph.** Usage-derived barriers were the right call. Extend it;
  do not replace it.
- **Content-addressed DDC.** Real content addressing (hashes file contents).
  Only the build key is missing.
- **Declared spawns in `mode.json`.** Data before scripts — this is what makes
  cross-gametype travel work.
- **The gate discipline.** 85 gates, 8267 assertions, 0 fail. Every pad adds
  one.

---

## Sources

[1] https://www.gdcvault.com/play/1024001/-Overwatch-Gameplay-Architecture-and — GDC Vault: 'Overwatch' Gameplay Architecture and Netcode (Timothy Ford, GDC 2017)
[4] https://gdcvault.com/play/1022187/Parallelizing-the-Naughty-Dog-Engine — Parallelizing the Naughty Dog Engine Using Fibers (GDC 2015)
[5] https://media.gdcvault.com/gdc2015/presentations/Gyrling_Christian_Parallelizing_The_Naughty.pdf — Gyrling, Parallelizing the Naughty Dog engine using fibers (slides)
[6] https://gdcvault.com/play/1022164/Multithreading-the-Entire- — Multithreading the Entire Destiny Engine (GDC 2015)
[8] https://gdcvault.com/play/1012200/Three-Big-Lies-Typical-Design — Three Big Lies: Typical Design Failures in Game Programming (GDC 2010)
[13] https://gdcvault.com/play/1023309/Building-a-Low-Fragmentation-Memory — Building a Low-Fragmentation Memory System for 64-bit Games (GDC 2016)
[14] https://media.gdcvault.com/gdc2016/Presentations/MacDougall_Aaron_Building_A_Low.pdf — MacDougall, Building a Low-Fragmentation Memory System for 64-bit Games (slides)
[15] https://www.gdcvault.com/play/1034234/The-Job-System-in-Cyberpunk — The Job System in Cyberpunk 2077 (GDC 2024)
[18] https://cppcon.org/third-keynote-2014 — Mike Acton, Data-Oriented Design and C++ (CppCon 2014 keynote)
[19] https://www.slideshare.net/slideshow/gdc15-code-clinic/45590473 — Acton, GDC15 Code Clinic slides
[20] https://gpuopen.com/download/gdc_2018_tutorial_memory_management_vulkan_dx12.pptx — Memory Management in Vulkan and DX12 (GDC 2018 slides)
[24] https://gdcvault.com/play/1026013/GPU-Driven-Rendering-and-Virtual — GDC 2019, Oleksandr Drazhevskyi (Ubisoft), "GPU Driven Rendering and Virtual Texturing in
[25] https://www.gdcvault.com/play/1025132/Frostbite-GPU-Emitter-Graph — GDC 2018, Sebastien Hillaire (EA) / Anders Egleus (DICE), "Frostbite GPU Emitter Graph Sys
[26] https://www.gdcvault.com/play/1025068/Beyond-Emitters-Shader-and-Surface — GDC 2018, Christina Coffin, "Beyond Emitters: Shader and Surface Driven GPU Particle FX Te
[27] https://www.gdcvault.com/play/1026126/Vulkan-The-State-of-the — GDC 2019, Tom Olson (Arm) / Karl Schultz (LunarG), "Vulkan: The State of the Union" -- htt
[28] https://gdcvault.com/play/1034621/Maximizing-Graphics-Performance-with-Flexible — GDC 2023, "Maximizing Graphics Performance with Flexible Virtualized Geometry" (Virtuos) -
[29] https://gdcvault.com/play/1035744/Rip-Tear-Breaking-Down-the — GDC 2025, "Rip & Tear: Breaking Down the Rendering of 'DOOM: The Dark Ages'" -- https://gd
[30] https://gdcvault.com/play/1026083/Scalable-Real-Time-Global-Illumination — GDC 2019, "Scalable Real-Time Global Illumination for Large Scenes" (Gaijin, Enlisted) --
[31] https://gdcvault.com/play/1026722/RTXGI-Scalable-Ray-Traced-Global — GDC 2022, "RTXGI: Scalable Ray Traced Global Illumination in Real Time" (NVIDIA) -- https:
[32] https://www.gdcvault.com/play/1029042/Efficient-Software-Occlusion-Culling-on — GDC 2022, Wenhui Tao (NetEase), "Efficient Software Occlusion Culling on Mobile Platform i
[39] https://gpuopen.com/download/GDC2024_Mesh_Shaders_in_AMD_RDNA_3_Architecture.pdf — GDC 2024, Max Oberberger / Lou Kramer (AMD), "Mesh Shaders in AMD RDNA 3 Architecture" --
[40] https://gpuopen.com/download/GDC-2023-Temporal-Upscaling.pdf — GDC 2023, Stephan Hodes (AMD), "Temporal Upscaling Past, Present, and Future" (FSR 2.2) --
[41] https://gpuopen.com/learn/gdc-2024-workgraphs-drawcalls — AMD GPUOpen, "GDC 2024 Work graphs and draw calls -- a match made in heaven!" -- https://g
[42] https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf — SIGGRAPH 2021 Advances, Brian Karis / Rune Stubbe / Graham Wihlidal (Epic), "Nanite: A Dee
[44] https://media.gdcvault.com/gdc2023/Slides/Buildingnightcity_Tremblay_Charles.pdf — GDC 2023, Charles Tremblay (CD PROJEKT RED), "Building Night City: The Technology of Cyber
[45] https://media.gdcvault.com/gdc2024/Slides/GDC+slide+presentations/Viau_Gauthier_DX12+Memory+Management.pdf — GDC 2024, Gauthier Viau (Massive Entertainment), "DX12 Memory Management in Snowdrop on PC
[49] https://gdcvault.com/play/1034403/Applied-Mesh-Analysis-Automating-Distant — GDC 2024, Scott Kircher (Insomniac), "Applied Mesh Analysis: Automating Distant-City LODs
[54] https://www.gdcvault.com/play/1034229/Cross-Platform-Determinism-in-Warhammer — GDC Vault: Cross-Platform Determinism in Warhammer Age of Sigmar: Realms of Ruin (Bradley
[56] https://media.gdcvault.com/gdc2017/Presentations/Reed_Dan_NetworkingScriptedWeapons.pdf — Slides: Networking Scripted Weapons and Abilities in 'Overwatch' (Dan Reed, GDC 2017)
[57] https://en.wikipedia.org/wiki/GGPO — Wikipedia: GGPO
[58] https://words.infil.net/w02-netcode-p5.html — Fightin' Words: The Technical Side of Rollback Netcode
[59] https://www.snapnet.dev/blog/netcode-architectures-part-2-rollback — Snapnet: Netcode Architectures Part 2: Rollback
[60] https://gdcvault.com/play/1020452/Lessons-Learned-from-a-Decade — GDC Vault: Lessons Learned from a Decade of Audio Programming (Guy Somberg)
[61] https://gdcvault.com/play/1029063/Fantastic-Audio-Algorithms-and-Where — GDC Vault: Fantastic Audio Algorithms and Where to Find Them (Guy Somberg)
[62] https://gdcvault.com/play/1024086/Sounds-Deep-Spatial-Audio-from — GDC Vault: Sounds Deep: Spatial Audio from the Ground Up (Ian Kelly)
[63] https://gdcvault.com/play/1026342/Spatial-Audio-in-Budget-Cuts — GDC Vault: Spatial Audio in 'Budget Cuts': A VR Stealth Game (Kjellberg/Antani)
[66] https://www.gdcvault.com/play/1025471/8-Frames-in-16ms-Rollback — GDC Vault: 8 Frames in 16ms: Rollback Networking in 'Mortal Kombat' and 'Injustice 2'
[67] https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html — Gabriel Gambetta: Client-Side Prediction and Server Reconciliation
[69] https://docs.rs/crate/luau-src/latest/source/luau/docs/_pages/sandbox.md — Luau docs: Embedding a sandboxed Luau virtual machine
[71] https://gdcvault.com/play/1022074/In-Your-Hands-The-Character — GDC Vault: In Your Hands: The Character (of Watch_Dogs) (GDC 2015)
[72] https://gdcvault.com/play/1021309/Math-for-Game-Programmers-Inverse — GDC Vault: Math for Game Programmers: Inverse Kinematics (Gino van den Bergen)
[73] https://gdcvault.com/play/1015559/Dedicated-Servers-In-Gears-of — GDC Vault: Dedicated Servers In Gears of War 3: Scaling to Millions of Players
[74] https://www.gdcvault.com/play/1036032/-Ragnarok-Online-3-The — GDC Vault: 'Ragnarok Online 3': Elastic MMO Cloud Architecture Supporting Million CCU
[78] https://gamedev.net/forums/topic/715041-server-side-client-command-extrapolation-and-reconciliation — GameDev.net: Server-side client command extrapolation and reconciliation (50ms resim windo
[79] https://gdcvault.com/play/1021981/Finding-Balance-Realizing-Responsive-High — GDC Vault: Finding Balance: Realizing Responsive High Fidelity Character Movement in Just
[81] https://www.gdcvault.com/play/1023480/Building-a-Video-Game — GDC Vault: Animation Bootcamp: Animation Microtalks (Blizzard/MDHR/DICE LA/etc.)
[82] https://www.gdcvault.com/play/1024009/Simple-and-Powerful-Animation — GDC Vault: Simple and Powerful Animation Compression (Nicholas Frechette, Eidos-Montreal,
[84] https://ruby0x1.github.io/machinery_blog_archive/post/the-machinery-asset-pipeline — Tobias Persson, "The Machinery Asset Pipeline", Our Machinery, https://ruby0x1.github.io/m
[86] https://ruby0x1.github.io/machinery_blog_archive/post/high-level-rendering-using-render-graphs — Tobias Persson, "High-Level Rendering Using Render Graphs", Our Machinery, 28 Aug 2017, ht
[88] https://ruby0x1.github.io/machinery_blog_archive/post/moving-the-machinery-to-bindless — Tobias Persson, "Moving The Machinery to Bindless", Our Machinery, 22 Feb 2021, https://ru
[94] https://aras-p.info/blog/2015/04/01/optimizing-unity-renderer-1-intro — Aras Pranckevičius, "Optimizing Unity Renderer Part 1: Intro", 1 Apr 2015, https://aras-p.
[96] https://aras-p.info/texts/files/201301%20Shader%20Pipeline%20in%20Unity.pdf — Aras Pranckevičius, "Unity's Shader Pipeline" (talk), Jan 2013, https://aras-p.info/texts/
[101] http://diaryofagraphicsprogrammer.blogspot.com/2018/03/triangle-visibility-buffer.html — Wolfgang Engel, "Triangle Visibility Buffer", Diary of a Graphics Programmer, 30 Mar 2018,
[104] https://wickedengine.net/category/devblog/2024/06/02/texture-streaming/index.html — János Turánszki, "Texture Streaming", Wicked Engine, 2 Jun 2024, https://wickedengine.net/
[112] https://bevy.org/news/bevy-0-16 — Bevy 0.16 release
[116] https://docs.o3de.org/docs/release-notes/archive/2505-0-release-notes — O3DE 25.05.0 release notes
[117] https://meshoptimizer.org/gltf — gltfpack meshoptimizer
[118] https://www.khronos.org/news/permalink/khronos-ktx-2.0-textures-enable-compact-visually-rich-gltf-3d-assets — Khronos KTX 2.0 Basis Universal
[119] https://www.khronos.org/gltf — Khronos glTF
[124] https://raw.githubusercontent.com/turanszkij/WickedEngine/master/features.txt — WickedEngine feature list
[126] https://github.com/BinomialLLC/basis_universal — Basis Universal GPU texture codec
[128] https://github.com/BinomialLLC/basis_universal/wiki/KTX2-File-Format-Support-Technical-Details — Basis Universal KTX2 support details
[129] https://docs.vulkan.org/samples/latest/samples/performance/texture_compression_basisu/README.html — Vulkan Basis Universal sample
[130] https://godotengine.org/releases/4.5 — Godot 4.5 release
[134] https://raw.githubusercontent.com/Unity-Technologies/JoltPhysics/master/Docs/ReleaseNotes.md — Jolt Physics release notes (raw)
[135] https://raw.githubusercontent.com/dimforge/rapier/master/CHANGELOG.md — Rapier changelog (raw)
[136] https://luau.org/sandbox — Embedding a sandboxed Luau virtual machine
[139] https://mujoco.readthedocs.io/en/stable/programming/simulation.html — Simulation - MuJoCo determinism and mj_step
[140] https://newton-physics.github.io/newton/latest/concepts/determinism.html — Deterministic Simulation - Newton Physics
[141] https://api.star-citizen.wiki/comm-links/18397 — Server Meshing and Persistent Streaming Q&A
[147] https://raw.githubusercontent.com/wolfpld/tracy/master/README.md — Tracy Profiler README
[148] https://raw.githubusercontent.com/pond3r/ggpo/master/doc/DeveloperGuide.md — GGPO Developer Guide
[149] https://doc.photonengine.com/quantum/v3/manual/quantum-ecs/fixed-point — Photon Quantum Fixed Point (Q48.16)
[150] https://github.com/bombomby/optick — Optick - C++ Profiler For Games
[151] https://salivity.github.io/game-development/article/cross-platform-deterministic-lockstep-architecture — Cross-Platform Deterministic Lockstep Architecture
[153] https://github.com/marcmicalizzi/game_engine/blob/main/docs/plan/03-data-model.md — 03 - Core Data and World Model (event-sourced)
[155] https://www.docs.o3de.org/docs/user-guide/profiling/cpu_profiling — CPU Profiling Support for Open 3D Engine
[157] https://nipsapp.com/procedural-content-generation-using-ai-in-games — Procedural Content Generation Using AI In Games
[158] https://cbsoft.sbc.org.br/2026/data/papers/workshops/QuestGuard%20Quality%20Gates%20and%20Diversity-Aware%20Repair%20for%20Reliable%20LLM-Generated%20Game%20Quests.pdf — QuestGuard: Quality Gates for LLM-Generated Game Quests
[164] https://create.roblox.com/docs/en-us/discovery — Roblox Creator Docs: Discovery
[165] https://about.roblox.com/newsroom/2026/06/optimizing-discovery-great-games-reach-millions-players-roblox — Optimizing Discovery: How Great Games Reach Millions of Players (Roblox)
[166] https://about.roblox.com/newsroom/2025/07/roblox-ai-moderation-massive-scale — How Roblox Uses AI to Moderate Content on a Massive Scale
[167] https://docs.vrchat.com/docs/vrchat-safety-and-trust-system — VRChat Safety and Trust System
[169] https://www.fortnite.com/news/introducing-the-creator-economy-2-0 — Introducing the Creator Economy 2.0 by Epic Games
[172] https://metaverse-standards.org/domain-groups/3d-assets — Metaverse Standards Forum: 3D Assets
[173] https://accelbyte.io/blog/platform-native-vs-game-backend-cloud-saves — Platform-Native vs Game Backend Cloud Saves (AccelByte)
[175] https://uscode.house.gov/us/usc/119-120/t47/s230 — 47 U.S.C. 230: Protection for private blocking and screening
[176] https://en.help.roblox.com/hc/en-us/articles/115004647846-Roblox-Terms-of-Use — Roblox Terms of Use
[179] https://www.ftc.gov/legal-library/browse/rules/childrens-online-privacy-protection-rule-coppa — FTC: Children's Online Privacy Protection Rule (COPPA)
[181] https://github.com/harfbuzz/harfbuzz — HarfBuzz text shaping engine
[189] https://docs.godotengine.org/en/stable/tutorials/ui/index.html — User interface (UI) - Godot Docs
[191] https://docs.xbox.com/publishing/certification/console-cert-tested-requirements — Console Certification Tested Requirements
[192] https://dev.epicgames.com/documentation/unreal-engine/packaging-and-cooking-games-in-unreal-engine — Packaging and Cooking Games in Unreal Engine
[193] https://dev.epicgames.com/documentation/unreal-engine/cooking-and-chunking — Cooking and Chunking (Unreal docs)
[195] https://heroiclabs.com/blog/matchmaking-lobbies-parties — Matchmaking, Lobbies, and Parties (Heroic Labs)
[196] https://tug.org/TUGboat/tb47-1/tb145esfahbod-harfbuzz.pdf — HarfBuzz: The quest for universal text shaping (TUGboat)
[198] https://gdcvault.com/play/1036033 — Cutscene Pipeline in 'Love and Deepspace' (GDC Vault)
[202] https://www.gdcvault.com/play/1025261 — Terrain Rendering in 'Far Cry 5' (GDC Vault)
[204] https://gdcvault.com/play/1012510/Standardizing-the-Localization — Standardizing the Localization Process (GDC Vault)
[208] https://www.gdcvault.com/play/1028009/Technical-Artist-Summit-Bringing-the — Technical Artist Summit: Bringing the World to Your Shaders
[213] https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-visual-effects-in-niagara-for-unreal-engine — Creating Visual Effects in Niagara (Unreal docs)
[218] https://metaverse-standards.org/domain-groups/interoperable-characters-avatars — MSF: Interoperable Characters/Avatars Working Group
[220] https://docs.xbox.com/services/playfab/live-service-management/game-configuration/experiments/experimentation-keys — PlayFab: Experiments (A/B testing) keys
[221] https://create.roblox.com/docs/projects/version-history — Roblox Creator Docs: Version History
[232] https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-material-editor-user-guide — Unreal Engine Material Editor User Guide
[233] https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-overview — Blueprint Overview (Unreal docs)
[239] https://www.ftc.gov/news-events/news/press-releases/2026/02/ftc-issues-coppa-policy-statement-incentivize-use-age-verification-technologies-protect-children — FTC: COPPA Policy Statement on age verification technologies (Feb 2026)
[253] https://ruby0x1.github.io/machinery_blog_archive/post/the-machinery-network-frontier-part-1/index.html — The (Machinery) Network Frontier -- Part 1
[255] https://ruby0x1.github.io/machinery_blog_archive/post/writing-a-low-level-sound-system/index.html — Writing a Low-Level Sound System -- You Can Do It!
[257] https://ruby0x1.github.io/machinery_blog_archive/post/keyboard-focus-and-event-trickling-in-immediate-mode-guis/index.html — Keyboard Focus and Event Trickling in Immediate Mode GUIs
[263] https://fgiesen.wordpress.com/2026/08/25/memory-ordering-in-cpus — Memory ordering in CPUs (ryg blog)
[267] https://github.com/cloverwoodstudio/codex-game-dev-library/blob/main/guides/systems/replays-determinism.md — Replays and Determinism (codex-game-dev-library)
[274] https://dev.epicgames.com/community/learning/knowledge-base/6PxP/unreal-engine-understanding-shader-permutations — Understanding Shader Permutations (Unreal)
[283] https://www.gdcvault.com/play/1021826/Automated-Testing-and-Instant-Replays — Automated Testing and Instant Replays in Retro City Rampage (GDC Vault)
[284] https://www.gdcvault.com/play/1029379/Tools-Summit-TestMonkey-Automated-Testing — Tools Summit: TestMonkey: Automated Testing at Santa Monica Studio
[289] https://www.gdcvault.com/play/1025261/Terrain-Rendering-in-Far-Cry — Terrain Rendering in 'Far Cry 5' (GDC Vault)
[290] https://www.gdcvault.com/play/1034449/UIPainter-Tile-Based-UI-Rendering — UIPainter: Tile-Based UI Rendering in One Draw Call (GDC Vault)
[298] https://wickedengine.net/category/devblog/2022/09/23/animation-retargeting/index.html — Animation Retargeting (Wicked Engine)
[300] https://wickedengine.net/category/devblog/2022/04/23/shader-compiler-tools/index.html — Shader compiler tools (Wicked Engine)
[302] https://blog.molecular-matters.com/2015/08/12/what-happened-to-the-molecule-engine — What happened to the Molecule Engine?
[306] https://ruby0x1.github.io/machinery_blog_archive/post/recording-statistics/index.html — Recording Statistics - An Exercise in Minimalism
[308] https://ruby0x1.github.io/machinery_blog_archive/post/the-machinery-network-frontier-part-2/index.html — The (Machinery) Network Frontier -- Part 2
[314] https://ruby0x1.github.io/machinery_blog_archive/post/one-draw-call-ui/index.html — One Draw Call UI
[318] https://zeux.io/2020/01/22/learning-from-data — Learning from data (zeux)
[322] https://tomforsyth1000.github.io/papers/fast_vert_cache_opt.html — Linear-Speed Vertex Cache Optimisation (Tom Forsyth)
[335] http://www.gdcvault.com/play/1015415/x — Terrain in Battlefield 3: A Modern, Complete and Scalable System
[339] http://www.gdcvault.com/play/1020490/x — LAMS: Building a Localization Tool for Everyone
[360] http://www.gdcvault.com/play/1024231/x — 'Destiny' Shader Pipeline
[368] http://www.gdcvault.com/play/1024708/x — 'Ghost Recon Wildlands': Terrain Tools and Technology
[369] http://www.gdcvault.com/play/1025490/x — The Visual Effects Technology of 'Destiny'
[370] http://www.gdcvault.com/play/1025811/x — Tools Tutorial Day: 9 Things Learned While Building Frostbite's Cinematics Tools
[374] http://www.gdcvault.com/play/1026148/x — Shader Graph: Ready for Production (Presented by Unity Technologies)
[383] http://www.gdcvault.com/play/1027463/x — Advanced Graphics Summit: Boots on the Ground: The Terrain of 'Call of Duty'
[393] http://www.gdcvault.com/play/1029149/x — Google Developer Summit: Google Play Games on PC: Pass Certification in 30 Minutes (Presen
[398] http://www.gdcvault.com/play/1034186/x — Design with Localization in Mind: Approaches to Translation Damage Control
[401] http://www.gdcvault.com/play/1034228/x — Nanite GPU-Driven Materials
[402] http://www.gdcvault.com/play/1034365/x — Scaling Git Workflows for Game Development
[403] http://www.gdcvault.com/play/1034436/x — How LiveOps Allows Games to be Played as Intended, Always
[404] http://www.gdcvault.com/play/1034449/x — UIPainter: Tile-Based UI Rendering in One Draw Call
[413] http://www.gdcvault.com/play/1036045/x — Trust, Attest, Server-Authoritative: Modern iOS Anti-Cheat for Multiplayer (Presented by G
[414] https://ruby0x1.github.io/machinery_blog_archive/post/the-machinery-shader-system-part-3 — The Machinery Shader System (part 3)
[425] https://www.gdcvault.com/play/1036045/Trust-Attest-Server-Authoritative-Modern — Trust, Attest, Server-Authoritative: Modern iOS Anti-Cheat (GDC 2026)
[467] https://developer.nvidia.com/blog/how-to-run-isolated-tenant-kubernetes-clusters-on-shared-gpu-infrastructure — NVIDIA: Isolated tenant K8s clusters on shared GPU infrastructure
[468] https://agones.dev/site/docs/guides/best-practices — Agones Best Practices
[470] https://www.cloudflare.com/products/rate-limiting — Cloudflare Rate Limiting - API Abuse Prevention
[471] https://developers.cloudflare.com/cache/how-to/tiered-cache — Cloudflare Tiered Cache docs
[472] https://www.aepd.es/en/guides/videogames-recommendations-industry.pdf — AEPD: Recommendations and best practices for data protection in video games
[473] https://sre.google/workbook/alerting-on-slos — Google SRE Workbook - Alerting on SLOs
[475] https://docs.xbox.com/services/playfab/data-analytics/index — PlayFab data and analytics docs
[476] https://godotengine.org/article/introducing-the-godot-asset-store — Godot Asset Store intro
[477] https://about.roblox.com/newsroom/2023/07/vision-roblox-economy — Our Vision for the Roblox Economy
[478] https://dev.epicgames.com/docs/epic-online-services/eos-overview — Epic Online Services (EOS) Overview
[480] https://www.fenwick.com/insights/publications/mechanics-under-attack-traditional-video-game-mechanics-face-renewed-scrutiny — Fenwick - Video game mechanics renewed scrutiny
[481] https://unity.com/blog/engine-platform/new-asset-import-pipeline-for-speeding-up-asset-imports — Unity: The new Asset Import Pipeline - dependency-tracked, artifact-revision-hashed asset builds
[482] https://dev.epicgames.com/documentation/unreal-engine/cinematics-and-movie-making-in-unreal-engine?lang=en-US — Unreal Engine Cinematics and Movie Making (Sequencer)
[483] https://dev.epicgames.com/documentation/unreal-engine/multi-user-editing-overview-for-unreal-engine — Unreal Engine Multi-User Editing Overview
[484] https://dev.epicgames.com/documentation/unreal-engine/live-link-in-unreal-engine?lang=en-US — Unreal Engine Live Link - DCC to engine live streaming
[485] https://www.cgchannel.com/wp-content/uploads/2011/03/project_skyline_overview.pdf — Project Skyline: Games Technology for a Modern Pipeline (live-linked authored+runtime data)
[486] https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine — Unreal Engine: Data Driven Gameplay Elements (DataTables/DataAssets)
[487] https://dev.epicgames.com/documentation/en-us/unreal-engine/horde-in-unreal-engine — Horde in Unreal Engine (distributed build system + UBA)
[488] https://defold.com/manuals/hot-reload — Defold manual: Hot reloading resources
[489] https://www.strayspark.studio/blog/undo-ai-agent-changes-unreal-effect-journal-rollback — Ctrl-Z Is Not a Rollback Strategy: Undoing (Unreal transaction buffer)
[490] https://github.com/marcmicalizzi/game_engine/blob/main/docs/plan/07-content-pipeline.md — Asset and Content Pipeline: DAG derived-data graph, provenance, license allowlist (marcmicalizzi/game_engine)
[491] https://www.socratopia.app/library/game-code-anatomy-en/chapter-25 — Reversible Commands and the Ctrl-Z Move in Game Editors (command pattern)
[492] https://crates.io/crates/assetpipe — assetpipe: asset provenance, dependency graphs, validation, credits
[493] https://fyrosgamesstudio.com/industry/generative-ai-game-art-copyright — Generative AI Game Assets: Rights, Consistency and Risk
[494] https://www.gamesindustry.biz/loot-box-state-of-play-2024-another-trip-around-the-world-of-regulation — GamesIndustry.biz: Loot Box State of Play 2024 regulation survey
[495] https://www.edpb.europa.eu/sme/be-compliant/respect-individuals-rights_en — EDPB: Respect individuals rights \(erasure\)
[496] https://sre.google/workbook/error-budget-policy — Google SRE Workbook: Error Budget Policy
[497] https://www.redhat.com/en/blog/designing-multitenant-gpu-infrastructure-isolation-across-virtualization-and-kubernetes-platforms — Red Hat: Designing multitenant GPU infrastructure isolation
[498] https://github.com/nfrechette/acl — ACL Animation Compression Library (Nicholas Frechette)
[499] https://nfrechette.github.io/2018/07/21/acl_v1.0.0 — ACL v1.0 release: bit-aligned animation compression (Frechette blog)
[500] https://www.gdcvault.com/play/1023280/Motion-Matching-and-The-Road — GDC 2016 Motion Matching and The Road to Next-Gen Animation (Simon Clavet, Ubisoft)
[501] https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-2-animated-crowd-rendering — GPU Gems 3 Ch2: Animated Crowd Rendering (instanced skinned characters)
[502] https://recastnav.com — Recast Navigation: navmesh generation + Detour (Mikko Mononen)
[503] https://github.com/recastnavigation/recastnavigation — recastnavigation: Recast/Detour source (Mikko Mononen)
[504] https://graphics.cs.wisc.edu/Papers/1998/Gle98 — Retargetting Motion to New Characters (Michael Gleicher, SIGGRAPH 1998)
[505] https://www.gafferongames.com/post/snapshot_interpolation — Gaffer On Games: Snapshot Interpolation (Glenn Fiedler, Networked Physics)
[506] https://developer.valvesoftware.com/wiki/Latency_Compensating_Methods_in_Client/Server_In-game_Protocol_Design_and_Optimization — Valve: Latency Compensating Methods in Client/Server In-game Protocol (Yahn Bernier, GDC 2001)
[507] https://www.syntaqx.com/books/gaming/netcode — Netcode Fundamentals (syntaqx): UDP reliability, transport options ENet/KCP/QUIC/WebTransport
[508] https://star-citizen.wiki/Comm-Link:18397/en — Star Citizen: Server Meshing and Persistent Streaming Q&A (Replication Layer)
[509] https://dev.epicgames.com/community/learning/knowledge-base/kjm3/testing-profiling-and-debugging-unreal-engine-s-networking — Unreal: Testing, Profiling, and Debugging Networking (Networking Insights, lag/loss emulation)
[510] https://papers.nips.cc/paper_files/paper/2006/hash/f44ee263952e65b3610b8ba51229d1f9-Abstract.html — TrueSkill: A Bayesian Skill Rating System (Herbrich, Minka, Graepel, NIPS 2006)
[511] https://www.ee.ucl.ac.uk/lcs/previous/LCS2011/LCS1121.pdf — Spatial Interest Management in Networked Games (Anvari & Rio, UCL 2011)
[512] https://www.gafferongames.com/post/snapshot_compression — Gaffer On Games: Snapshot Compression (Glenn Fiedler, Networked Physics)
[514] https://www.gdcvault.com/play/1026712/Ragdoll-Motion-Matching — GDC 2022 Machine Learning Summit: Ragdoll Motion Matching (Simon Clavet, Ubisoft)
[515] https://www.riotgames.com/en/news/peeking-valorants-netcode — Riot: Peeking into VALORANT's Netcode (server-authoritative, buffering, tick)
[516] https://www.riotgames.com/en/news/demolishing-wallhacks-valorants-fog-war — Riot: Demolishing Wallhacks with VALORANT's Fog of War (interest/visibility)
[517] https://www.syntaqx.com/books/gaming/game-servers — Game Servers & Hosting (syntaqx): dedicated fleets, scaling, QoS
[518] https://godotengine.org/article/rendering-acyclic-graph — GPU sync in Godot 4.3: rendering acyclic graph (Banini)
[520] https://tonisagrista.com/blog/2023/sparse-virtual-textures — Sparse Virtual Textures (Toni Sagrista Gaia Sky)
[521] http://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range — Use DirectX with Advanced Color (Microsoft Learn)
[522] https://www.intel.com/content/www/us/en/developer/articles/technical/masked-software-occlusion-culling.html — Masked Software Occlusion Culling (Intel)
[523] https://courses.grainger.illinois.edu/CS418/sp2023/text/nanite.html — Nanite Streaming LOD Occlusion (UIUC CS418)
[524] https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html — Enhanced Barriers spec (DirectX-Specs)
[525] https://devblogs.microsoft.com/directx/a-look-inside-d3d12-resource-state-barriers — A Look Inside D3D12 Resource State Barriers (DirectX Dev Blog)
[526] https://www.shlom.dev/articles/how-virtual-textures-really-work — How Virtual Textures Really Work (shlom.dev)
[527] https://devblogs.microsoft.com/directx/d3d12-enhanced-barriers-preview — D3D12 Enhanced Barriers Preview (DirectX Dev Blog)
[528] https://learn.microsoft.com/en-us/windows/win32/direct3d12/multi-engine — Multiple Direct3D 12 engines/multi-adapter (MS Learn)
[529] https://docs.unrealengine.com/en-US/RenderingAndGraphics/GlobalIllumination/index.html — Lumen Technical Details (Unreal docs)
[531] https://www.unrealengine.com/tech-blog/lumen-brings-real-time-global-illumination-to-fortnite-battle-royale-chapter-4 — Lumen in Fortnite BR Chapter 4 (Epic tech blog)
[532] https://gpuopen.com/download/Practical_DX12_Programming_Model_and_Hardware_Capabilities.pdf — Practical DirectX 12: Programming Model and Hardware Capabilities (GDC 2016)
[534] https://gpuopen.com/download/GDC_2016_D3D12_Right_On_Queue_final.pdf — Right on Queue: Advanced DirectX 12 Programming (GDC 2016)
[536] https://www.simoncoenen.com/blog/programming/graphics/DoomEternalStudy.html — DOOM Eternal Graphics Study (Simon Coenen)
[537] https://en.wikipedia.org/wiki/Clipmap — Clipmap - Wikipedia (Megatexture)
[540] https://gafferongames.com/post/fix_your_timestep — Glenn Fiedler, "Fix Your Timestep!", Gaffer On Games, 2004
[541] https://www.gafferongames.com/post/deterministic_lockstep — Glenn Fiedler, "Deterministic Lockstep", Gaffer On Games
[542] https://www.digitalsignallabs.com/fp.pdf — Randy Yates, "Fixed-Point Arithmetic: An Introduction", Digital Signal Labs (2007)
[545] https://arxiv.org/abs/1608.03676 — Curtsinger & Berger, "Coz: Finding Code that Counts with Causal Profiling" (SOSP 2015)
[548] https://www.akkadia.org/drepper/cpumemory.pdf — Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007)
[552] https://ruby0x1.github.io/machinery_blog_archive/post/fiber-based-job-system/index.html — Our Machinery, "Fiber based job system" (Tobias Persson)
[554] https://preshing.com/20120913/acquire-and-release-semantics — Jeff Preshing, "Acquire and Release Semantics" (2012)
[556] https://developer.nvidia.com/performance-rendering-tools/reflex — NVIDIA Reflex SDK (official documentation)
[557] https://developer.nvidia.com/blog/understanding-and-measuring-pc-latency — Seth Schneider, "Understanding and Measuring PC Latency", NVIDIA Technical Blog (2023)
[559] https://google.github.io/benchmark/user_guide.html — Google Benchmark, official user guide (statistical benchmarking methodology)
[562] https://salivity.github.io/game-development/article/how-to-debug-network-game-desyncs-across-machines — How to Debug Network Game Desyncs Across Machines (salivity)
[563] https://mightyprofessionalgaming.com/tutorials/memory-allocators-from-scratch.html — Memory Allocators from Scratch (Mighty Professional Gaming)
[564] https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/en/vol6-performance/ch01-benchmark-methodology/01-why-microbenchmarks-lie — Why Microbenchmarks Lie (Modern C++ Tutorial)
[565] https://arm-software.github.io/magog/docs/98_appendices/01_writing_benchmarks — Writing benchmarks (Arm Mobile Application GPU Optimization Guide)
[566] https://github.com/syntaqx/syntaqx.com/blob/main/content/books/gaming/determinism-and-rollback.md — Lockstep, Rollback and Determinism (syntaqx)
[567] https://gdcvault.com/play/1022929/Unified-Telemetry-Building-an-Infrastructure — Unified Telemetry, Building an Infrastructure for Big Data in Games Development (Maurizio De Pascale, Ubisoft Montreal, GDC 2016)
[571] https://arxiv.org/html/1608.03676v1 — Coz: Finding Code that Counts with Causal Profiling (Curtsinger and Berger, SOSP 2015) full text
[573] https://www.ogc.org/standards/dggs — OGC DGGS Standard | Discrete Global Grid Systems
[574] https://agones.dev/site/docs/reference/gameserverallocation — Agones GameServerAllocation Specification
[576] https://github.com/CesiumGS/3d-tiles — CesiumGS/3d-tiles: 3D Tiles specification repo
[577] https://gafferongames.com/post/packet_fragmentation_and_reassembly — Glenn Fiedler, Packet Fragmentation and Reassembly (Networked Physics)
[578] https://developer.mozilla.org/en-US/docs/Web/API/WebTransport_API — MDN Web Docs: WebTransport API
[579] https://datatracker.ietf.org/doc/html/rfc9000 — RFC 9000: QUIC - A UDP-Based Multiplexed and Secure Transport (Iyengar & Thomson, IETF 2021)
[580] https://www.lncc.br/~jauvane/papers/VELVET-ICC2002.pdf — VELVET: an adaptive hybrid architecture for very large virtual environments (de Oliveira et al., ICC 2002)
[581] https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking — Valve Developer Community: Source Multiplayer Networking
[582] https://www.ogc.org/standards/cdb — OGC CDB Standard - Synthetic Environment Data Model & Structure
[583] https://www.openstreetmap.org/copyright/en — OpenStreetMap Copyright and License (ODbL)
[584] https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/capacity-reservation-overview.html — AWS On-Demand Capacity Reservations and Capacity Blocks for ML
[585] https://wiki.secondlife.com/wiki/About_Land — Second Life Wiki: About Land
[586] https://create.roblox.com/docs/projects/teleport — Roblox Creator Docs: Teleport (reserved servers)
[587] https://create.roblox.com/docs/en-us/production/publishing/publish-games-and-places — Roblox Creator Docs: Create and publish games and places (experience/place model)
[588] https://create.roblox.com/docs/en-us/workspace/streaming — Roblox Creator Docs: Instance streaming
[589] https://wiki.secondlife.com/wiki/Introduction_To_Second_Life_Geography — Second Life Wiki: Introduction To Second Life Geography (grid/continent/sim)
[591] https://minecraft.wiki/w/World — Minecraft Wiki: World (world/dimension)
[593] https://learn.microsoft.com/en-us/bingmaps/articles/bing-maps-tile-system — Microsoft Learn: Bing Maps Tile System (quadkey/LOD)
[594] https://s2geometry.io/devguide/s2cell_hierarchy.html — S2Geometry: S2 Cells (cell hierarchy)
[595] https://www.uber.com/us/en/blog/h3 — Uber Engineering: H3 Hexagonal Hierarchical Spatial Index
[596] https://www.eveonline.com/news/view/tranquility-tech-iv — EVE Online: Tranquility Tech IV (cluster/node/system)
[597] https://create.roblox.com/docs/projects/client-server — Roblox Creator Docs: Client-server runtime
[598] https://warcraft.wiki.gg/wiki/Instance — Warcraft Wiki: Instance
[599] https://wiki.secondlife.com/wiki/Land — Second Life Wiki: Land (parcel/region/estate/grid)
[600] https://create.roblox.com/docs/en-us/reference/engine/classes/TeleportService — Roblox Creator Docs: TeleportService API (place/instance addressing)
[602] https://create.roblox.com/docs/en-us/cloud-services/data-stores — Roblox Creator Docs: Data stores (per-game persistence)
[603] https://drops.dagstuhl.de/storage/00lipics/lipics-vol361-opodis2025/html/LIPIcs.OPODIS.2025.11/LIPIcs.OPODIS.2025.11.html — Formalizing Rollback Netcodes for Robust and Real-Time Client-Server Multiplayer Games (OPODIS 2025)
[604] https://www.wolfire.com/blog/2011/03/GDC-Session-Summary-Halo-networking — Wolfire: GDC Session Summary - Halo networking (David Aldridge, I Shot You First, GDC 2011)
[606] https://agones.dev/site/docs — Agones Documentation (dedicated game server hosting/scaling on Kubernetes)
[607] https://edgegap.com/platform/technology-explainer-game-server-hosting-orchestration — Edgegap: How game server orchestration works (regionless edge, ideal-server placement)
[608] https://www.wireguard.com/protocol — WireGuard: Protocol & Cryptography (Noise_IK, ChaCha20-Poly1305 over UDP)
[609] https://6it.dev/blog/udp-for-games--security-encryption-and-ddos-protection-1213 — UDP for games: security (encryption and DDoS protection)
[610] https://www.ntp.org/ntpfaq/NTP-s-algo — NTP FAQ: How does it work? (clock synchronization algorithm)
[611] https://www.linux.org/docs/man8/tc-netem.html — NetEm (tc-netem) - Linux network emulator / traffic shaping
[612] https://daily.dev/posts/how-to-think-about-game-servers-pt-2---hernan-rivas-acosta-erlang-solutions-webinar-kuzpbm8qr — Hernan Rivas Acosta: How to Think About Game Servers Pt 2 (clock sync, lag compensation)
[613] https://datatracker.ietf.org/doc/html/rfc8445 — RFC 8445: Interactive Connectivity Establishment (ICE) - NAT traversal
[614] https://datatracker.ietf.org/doc/html/rfc9221 — RFC 9221: An Unreliable Datagram Extension to QUIC
[616] https://github.com/microsoft/globalmlbuildingfootprints — Microsoft Global ML Building Footprints
[618] https://www.ogc.org/initiatives/ai-dggs-pilot — OGC AI-DGGS Pilot: Mapping the World with Smart Grid Cells
[621] https://docs.ogc.org/as/20-040r3/20-040r3.html — OGC Topic 21 - Discrete Global Grid Systems (Abstract Specification)
[622] https://astrogeology.usgs.gov/search/map/mars_mgs_mola_dem_463m — USGS Astropedia: Mars MGS MOLA DEM 463m
[623] https://github.com/Krilliac/SparkEngine/wiki/Engine-and-Renderer-Landscape — SparkEngine wiki: Engine and Renderer Landscape (SpatialOS authority/interest, server meshing)
[624] https://github.com/PageMastr/HeliosEngine/blob/main/docs/research/07-mmo-backend-architecture.md — HeliosEngine: MMO backend architecture (epoch-fenced authority handoff, ghost entities)
[626] https://www.youtube.com/watch?v=h47zZrqjgLc — GDC 2011: David Aldridge (Bungie), I Shot You First - Halo: Reach netcode
[629] https://gafferongames.com/post/state_synchronization — Glenn Fiedler, State Synchronization (Networked Physics)
[630] https://create.roblox.com/docs/cloud/reference/features/universes — Roblox Creator Docs: Universes API reference (universe/place addressing)
[631] https://agones.dev/site/docs/faq — Agones FAQ (scale targets, GameServer lifecycle)
