# WWW — Experience & Gap Assessment

**Question:** the engine has ~90 modules and 88 green gates. Is it a functional,
cohesive **experience** — for a player, and for a creator — and if not, what is
missing and how do we validate it?

**Method:** measured the tree (module inventory, include graph, link graph, gate
output, actual program runs). No claims below are from memory; each number is
reproducible from the commands in §9.

---

## 1. TL;DR

1. **The engine is a capability library, not a product.** 92 modules, 17k LOC of
   core C11, 88 gates / 8,457 assertions, 0 fail — and **only 11 of 92 modules are
   reachable from the shipped host**. The renderer, GPU backend, physics, assets,
   avatars, the quadkey spatial core and the tile store are all *not on the path*
   the shipped binary runs.
2. **The player experience is a 148-tick tech demo.** `make run` prints three
   lines: spawn, auto-walk to a rift, travel, exit. There is no input, no camera,
   no objective, no win or lose, no render, no physics, no animation, no audio.
3. **The creator experience is real but bare.** A creator hand-writes
   `mode.json` + `main.lua` in a directory and runs a binary. The manifest is
   validated; nothing else is. No feedback, no reload, no editor, no publish.
4. **The two are one loop and must be validated as one.** Creators make places,
   players play them, players become creators. Every mechanism in §5 is a check on
   that loop, not on a module.
5. **The blocking work is composition and validation, not more modules.** The
   modules exist. They are not connected to each other or to an experience, and
   nothing checks that an experience works.
6. **Two pieces of architectural debt are already visible:** two incompatible
   quadkey representations (fixed in the linker, not unified), and a dead
   abstraction (`wb_exp`) still linked into the binary.

---

## 2. Measured current state

### 2.1 Size

| What | Number |
|---|---|
| Core `.c` / `.h` | 97 / 92 |
| Core LOC | 11,453 + 5,553 = **17,006** |
| Other src (`lua`, `net`, `lv`) | 1,053 + 1,028 + 6,807 LOC |
| Gates | **88 results / 8,457 assertions / 0 fail** |
| Test sources | 86 |
| Pads | 113 → **9 shipped, 47 partial, 57 todo** |

### 2.2 The wiring, measured

Built the include graph over `src/` and classified every module header:

| Class | Count | Meaning |
|---|---|---|
| **Wired in** | 34 | consumed by at least one other module |
| **Leaf** | 56 | only its own `.c` includes it — a capability with no consumer |
| **Unwired** | 2 | nothing in `src/` includes it (`wb_atomic.h`, `wb_fastmath.h`, both header-only, referenced only by tests) |

Then the decisive one — breadth-first from the real entry points (`wb_host`,
`wb_worldspace`, `wb_script`, `wb_lua_host`, `wb_space_bind`):

> **11 of 92 modules are reachable from the shipped path.**
> `wb_dist, wb_ecs, wb_host, wb_json, wb_lua_host, wb_mode, wb_script,
> wb_space_bind, wb_spawn, wb_util, wb_worldspace`

Everything else — `wb_render`, `wb_gpu*`, `wb_physics*`, `wb_assets*`,
`wb_avatar*`, `wb_spatial`, `wb_tiles`, `src/net/*`, `wb_input`, `wb_camera`,
`wb_character`, `wb_edit`, `wb_heightfield`, all 15 of the nesting/geo layer — is
**not reachable** from the host. It is linked into the binary and never called.

### 2.3 Dead and duplicated

- **`wb_exp` is dead.** The "experience container — one Lua state + ECS each" is
  referenced by nothing outside `wb_exp.c` and `tests/wb_exp_test.c`. Instance
  management moved to `wb_dist`; `wb_exp.o` is still linked into every binary.
- **Two quadkey representations.** `wb_spatial.h` defines
  `typedef uint64_t wb_qkey` (morton handle, level in bits 63..60).
  `wb_qkey.h` (LP-108) speaks a `const char *` of `'0'..'3'`. Nothing converts
  between them. They collided in the linker until `d5773c9`.

### 2.4 What actually runs

```
$ ./build/wubu_engine.exe games omniverse-hub
wb: in 'omniverse-hub' as player-1 (instance exp-1)
hub: player-1 stepped through rift 0 into instance exp-2 at (0, 0, 0)
wb: engine exited clean after 148 ticks

$ ./build/wubu_engine.exe games duel-stadium
wb: in 'duel-stadium' as player-1 (instance exp-1)
stadium: player-1 is at (0, 0, 0)
wb: engine exited clean after 120 ticks
```

The player is **scripted, not played**: the hub's `main.lua` sets a velocity and
walks the entity into its own gate. There is no window, no input, no objective,
no score, no end condition. The whole of the shipped player experience is three
printed lines.

---

## 3. The two experiences

### 3.1 The player experience, as it exists

| Stage | Present? | Evidence |
|---|---|---|
| Join a world | **yes** | `wb_worldspace_spawn` → instance |
| Appear somewhere | **yes** | declared spawns in `mode.json` |
| Move | **no** | no input wiring; the demo sets velocity from a script |
| See the world | **no** | renderer not reachable from the host |
| Have an objective | **no** | no win/lose/score/timer anywhere |
| Feel it | **no** | P2 (animate + emit) is 0/10, entirely TODO |
| Leave / replay | **partial** | travel between instances exists; no return, no replay |
| Progress | **no** | persistence is per-volume (LP-102), not per-player |

### 3.2 The creator experience, as it exists

| Stage | Present? | Evidence |
|---|---|---|
| Declare a gametype | **yes** | `mode.json` + typed config schema (`wb_mode`) |
| Write gameplay | **yes** | Lua entry script, `wb.*` bindings |
| Declare spawns | **yes** | in the manifest, before any script runs |
| Wire travel | **yes** | `wb.add_gate` / `travel` (slice 21) |
| Validate it | **partial** | manifest schema only |
| Run it | **yes** | `wubu_engine.exe games <dir>` |
| See why it failed | **no** | stdout only, no diagnostics model |
| Reload without restarting | **no** | |
| Edit the world visually | **no** | `wb_edit` exists and is a leaf |
| Publish it | **no** | |
| See anyone play it | **no** | |
| Get credited | **no** | LP-79 is asset-level, not creator-level |

### 3.3 Why they are one experience

A player who cannot become a creator is a consumer; a creator whose work no
player reaches is shouting into a void. The loop is:

```
creator builds → player plays → player reacts → player becomes creator
```

Every platform that won at this did not build two products. Roblox, Minecraft,
VRChat, Core and Second Life are one loop each, and the loop is the product.
Concretely for WWW that means:

- **a creator change must be validated through the player path** — otherwise the
  creator is flying blind;
- **a player change must be visible to the creator** — otherwise the creator is
  building for nobody;
- therefore **the unit of validation is the loop, not the module.**

This is the organising idea for everything in §5 and §6.

---

## 4. Gap analysis

### 4.1 Composition — the host does not reach the engine

The single largest gap. The shipped binary touches 11 modules; 81 are off-path.
This is not a criticism of the modules — most are genuinely good — it is the
difference between a library and a product:

- **No render path.** `wb_render` (software rasteriser) and `wb_gpu*` (D3D12 RHI)
  are leaves. `make demo` runs the rasteriser as a separate example program; the
  host never draws a frame.
- **No physical path.** `wb_physics*` (MuJoCo world/body/bind) are leaves.
- **No spatial path.** `wb_spatial` and `wb_tiles` (the quadkey spine and the
  resident tile store) are leaves from the host. The world has no terrain and no
  streaming in the shipped path.
- **No avatar path.** `wb_avatar*` are leaves; there is no body to inhabit.
- **No net path.** `src/net/*` and `wb_netmodel`/`wb_handoff`/`wb_match` are
  leaves; the shipped experience is single-process and local.
- **No input path.** `wb_input`, `wb_camera` and `wb_character` are leaves —
  which is why the demo has to drive the player with a script.

### 4.2 Player-experience gaps

Ordered by how early they block the experience:

1. **Input → movement.** Nothing translates a device into an entity. (`wb_input`,
   `wb_camera`, `wb_character` exist; none is wired.)
2. **A view.** No window, no camera, no draw call in the host path.
3. **An objective.** No win/lose/score/timer abstraction exists at all. A game
   cannot be authored yet — only a place.
4. **Onboarding.** Nothing defines what a player does in the first 30 seconds.
5. **Feel.** Animation (P2), audio (P2), UI (`LP-42`, XL) are all TODO.
6. **Progress.** No per-player persistence, no identity, no return visit.

### 4.3 Creator-experience gaps

1. **Validation feedback.** Only the manifest is checked. A mode that boots and
   then dead-ends is indistinguishable from a good one.
2. **A repeatable run.** Running a mode is a manual command; nothing records what
   happened.
3. **Fast iteration.** No reload; every change is a process restart.
4. **A publish step.** No packaging, no version, no distribution.
5. **Creator analytics.** No "did anyone play it, did they finish".
6. **Onboarding.** No first-run, no template, no example beyond two demos.

### 4.4 Validation gaps — the actual subject of this assessment

The 88 gates are **module gates**. They prove each capability works in isolation.
They cannot answer any of these:

- Does a *place* work?
- Can a *player* finish it?
- Can a *creator* get from an idea to a running place safely?
- Did a player who arrived *stay*?

There is no end-to-end acceptance, no automated playtest, no creator-workflow
check, and no experience telemetry. **The engine is verified at the wrong level
of abstraction** — which is exactly why 88 green gates coexist with a binary
that was, until today, not linkable.

---

## 5. How to validate both experiences

### 5.1 The principle

> Validate the **loop**, not the module. A check that cannot fail when the
> player cannot finish the place is not an experience check.

Every mechanism below is a gate that runs in CI and fails when the loop breaks.

### 5.2 The mechanisms

**M1 — The vertical-slice gate.** One command (`make slice`) that builds, runs a
creator-made world, and asserts the whole path: boot → spawn → act → objective →
exit. This already exists in embryo: `make run` plus three printed lines. Turn it
into an assertion. *Exit criterion: `make slice` is red if the loop breaks.*

**M2 — Experience gates on a place.** `wb_placeval` (LP-112) already validates a
place's structure, containment, source DEM and licences. Extend it from "is this
place well-formed?" to "is this place an experience?":

- it has at least one spawn, and the spawn is reachable;
- it has an objective (a win or lose condition) and an end;
- the objective is achievable — proven by a bot, not asserted by the author;
- it has an exit or a next step (no dead ends);
- it is completable within a time bound.

*Exit criterion: a place that cannot satisfy these cannot be published.*

**M3 — Bot playtests.** The hub's `main.lua` is already a scripted player: it
walks an entity toward a goal and fires an event. Generalise it into a bot that
must complete each gametype in `games/`, headlessly, in CI. Assert completion,
assert no dead ends, assert no crash, assert the tick bound.

*Exit criterion: every shipped gametype has a bot that finishes it.*

**M4 — The creator-workflow gate.** For each gametype: `validate → run → assert
boot → assert the bot finishes`. This is M3 from the creator's side and it is what
makes a creator's change *safe* — the same guarantee the module gates give a C
change. *Exit criterion: a creator runs one command and gets pass/fail with a
reason.*

**M5 — Experience telemetry.** `wb_prof` (profiler) and `wb_eventlog` (snapshot +
tail log) exist and are leaves. Record the loop as events — `join`, `spawn`,
`first_action`, `objective`, `leave` — and assert on the sequence and the timing.

*Exit criterion: a run produces an event trace; a gate asserts the trace is
complete.*

**M6 — Derived experience heuristics.** From M3/M5, cheap and brutal:

- **time to first action** ≤ N seconds (onboarding);
- **no unrecoverable state** in the first M metres;
- **completion rate** of the bot = 100% (a bot is the worst player);
- **tick budget** never exceeded.

*Exit criterion: a regression in any of these fails the build.*

**M7 — The flywheel gate (the "one experience" check).** A creator edit must be
validated through the player path: *edit → bot plays → creator-visible result.*
This is the only mechanism that tests both experiences at once, and therefore the
only one that tests the product. *Exit criterion: a change to a gametype cannot
land without a bot having played the result.*

### 5.3 What each mechanism catches

| Breaks | Caught by |
|---|---|
| A place is malformed | M2 (already shipped as LP-112) |
| A place cannot be finished | M2, M3 |
| A creator's change breaks the game | M4, M7 |
| The loop got slower / dead-ends | M5, M6 |
| A player cannot become a creator | M7 |
| A module regresses | the existing 88 gates |

The module gates are not replaced. They are **necessary and insufficient** — and
saying so is the point.

---

## 6. Prioritised roadmap

Each phase has an exit criterion that is a *running program*, not a document.

### Phase 0 — Make the binary honest *(one commit, done)*
- Disambiguate the two quadkey APIs so `make all` links — **done, `d5773c9`**.
- Resolve `wb_exp`: delete it, or make it the live path. It is dead code today.
- Turn `make run` into `make slice` with assertions.
- **Exit:** `make all` links, `make slice` passes, and a stale-run check fails CI.

### Phase 1 — Composition: put the engine in the host
Wire, in order, each currently-unreachable subsystem into the shipped path:
**render → input/camera/character → spatial/tiles → physics → assets/avatar**.
This is largely inverting the existing leaf modules, not writing new ones.
- **Exit:** `make run` opens a window, draws a frame from the world, and the
  player moves under input. No new gameplay.

### Phase 2 — The player loop
Add the missing stages: **objective** (win/lose/score/timer), **onboarding**,
**animation and audio** (all of P2, currently 0/10), **UI/HUD** (`LP-42`).
- **Exit:** a bot completes a full loop with an objective, and M2/M3 pass on it.

### Phase 3 — The creator loop
**Validation feedback** (M4), **hot reload**, **publish**, **creator analytics**
(M5 surfaced to the creator).
- **Exit:** a creator goes from an empty directory to a published, validated,
  playable place with one command, and can see that a bot finished it.

### Phase 4 — The flywheel
Player telemetry reaches the creator; players can fork/remix a place they played
(the player→creator step); credits and discovery.
- **Exit:** M7 passes — a creator edit triggers a bot play and the creator sees
  the result, in one command.

### Pads this draws on (already in `docs/WORKLIST.md`)
Phase 1: LP-22, LP-24, LP-25, LP-42, LP-60, LP-64 · Phase 2: LP-34, LP-50, LP-51
· Phase 3: LP-38, LP-39, LP-45, LP-74, LP-75, LP-78 · Phase 4: LP-57, LP-80,
LP-86.

---

## 7. Honest risks

1. **Composition is not free.** Wiring 81 modules into one path will surface
   assumptions that the isolated gates cannot: lifetimes, frame budgets,
   threading, error propagation. Expect the first composed build to be unstable
   for a while. That is the work, not a setback.
2. **Two quadkey representations are still two.** Renaming fixed the linker; it
   did not unify the model. Every feature that needs both (a tile addressed by
   handle *and* by digit string — i.e. streaming a named place) will hit this.
   It should be a pad of its own, not a series of local workarounds.
3. **Experience gates can become theatre.** A bot that always finishes proves
   nothing if the bot is written to pass. The heuristics in M6 exist to keep the
   bots honest, and they must be allowed to fail.
4. **The player and creator paths can be optimised apart.** If the creator tools
   are validated only by "did it build" and the player path only by "did it
   render", the loop is never tested and the product never coheres. M7 is the
   guard against this; it should be built early, not last.
5. **Scope.** This is a multi-month programme. Phase 1 alone is larger than any
   single pad so far. It should be sequenced as pads with exit criteria, not as
   one programme.

---

## 8. Sources

Grounded in the project's own citation ledger (`docs/LILYPADS.md`, 631 sources)
and in the assessment research pass:

**Player-experience and engine practice**
- [145] GDC — *Overwatch* Gameplay Architecture and Netcode (Timothy Ford, GDC 2017)
- [73] GDC — Dedicated Servers in Gears of War 3: Scaling to Millions of Players
- [151] Cross-Platform Deterministic Lockstep Architecture
- [147][150][155][306] Tracy / Optick / statistics-ring practice (instrumentation
  as the substrate for experience measurement)
- [611] Anvari & Rio, *Spatial Interest Management in Networked Games* (UCL 2011)

**Creator workflow and validation**
- [490] marcmicalizzi/game_engine — *Asset and Content Pipeline*: DAG derived-data
  graph, **provenance, licence allowlist**
- [492] `assetpipe` — asset provenance, dependency graphs, **validation**, credits
- [488] Defold — hot reloading resources
- [195] Heroic Labs — matchmaking, lobbies and parties
- [196] HarfBuzz (text shaping as a creator-facing dependency)
- [481] Unity — asset import pipeline, dependency-tracked, revision-hashed builds
- [487] Unreal — Horde distributed build system
- [483] Unreal — multi-user editing overview

**UGC platforms and the flywheel**
- [586][587][588] Roblox — Teleport / publishing / instance streaming
- [591] Minecraft Wiki — World (world/dimension) persistence
- [596] EVE Online — Tranquility Tech IV (one node, one solar system)
- [598] Warcraft Wiki — Instance
- [599] Second Life Wiki — Land (parcel/region/estate/grid)
- [167] VRChat — Safety and Trust System
- [618] OGC AI-DGGS Pilot — DGGS as the authoritative content store for AI/RAG
- [173] AccelByte — Platform-Native vs Game Backend Cloud Saves

**Places, proof and provenance (already shipped this session)**
- LP-79 `wb_prov` — asset provenance + SPDX allowlist
- LP-110 `wb_geolic` — per-cell open-data licensing
- LP-112 `wb_placeval` — the publish gate this assessment proposes extending
- LP-113 `wb_quota` — per-volume capacity and quotas
- LP-43 `wb_heightfield` — terrain representation

---

## 9. Reproduce the measurements

```bash
cd WWW/~WuBu~

# size
find src/core -name 'wb_*.c' | wc -l
find src/core -name 'wb_*.h' | wc -l
wc -l src/core/*.c src/core/*.h | tail -1

# gates
mingw32-make test            # 88 gate results / 8,457 assertions / 0 fail

# the shipped path
mingw32-make all && ./build/wubu_engine.exe games omniverse-hub

# module wiring / reachability
python3 tools/wiring_report.py        # leaf vs wired vs unwired, and reachability
```
