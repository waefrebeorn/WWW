# WWW — Experience & Gap Assessment

**Question:** the engine has ~90 modules and 88 green gates. Is it a functional,
cohesive **experience** — for a player, and for a creator — and if not, what is
missing and how do we validate it?

**Method:** measured the tree (module inventory, include graph, link graph, gate
output, actual program runs). No claims below are from memory; each number is
reproducible from the commands in §9.

---

## 1. TL;DR

1. **The engine is a capability library, not a product.** 94 modules, 18k LOC of
   core C11, 90 gates / 8,544 assertions, 0 fail — and **only 20 of 94 modules are
   reachable from the shipped host**. The GPU backend, physics, assets,
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
   quadkey representations (fixed in the linker, not unified), and a
   determinism cluster — `wb_snap`, `wb_interp`, `wb_prof`, `wb_det` — that is
   reachable only through `wb_exp`, an abstraction the shipped host never calls.

---

## 2. Measured current state

### 2.1 Size

| What | Number |
|---|---|
| Core `.c` / `.h` | 99 / 94 |
| Core LOC | 12,131 + 5,851 = **17,982** |
| Other src (`lua`, `net`, `lv`, `platform`) | 1,190 + 1,028 + 6,807 + 292 LOC |
| Gates | **90 results / 8,544 assertions / 0 fail** |
| Test sources | 87 |
| Pads | 114 → **9 shipped, 48 partial, 57 todo** |

### 2.2 The wiring, measured

Built the include graph over `src/` and classified every module header:

| Class | Count | Meaning |
|---|---|---|
| **Wired in** | 34 | consumed by at least one other module |
| **Leaf** | 56 | only its own `.c` includes it — a capability with no consumer |
| **Unwired** | 2 | nothing in `src/` includes it (`wb_atomic.h`, `wb_fastmath.h`, both header-only, referenced only by tests) |

Then the decisive one — breadth-first from the real entry points (`wb_host`,
`wb_worldspace`, `wb_script`, `wb_lua_host`, `wb_space_bind`):

> **20 of 94 modules are reachable from the shipped path.**
> `wb_camera, wb_character, wb_det, wb_dist, wb_ecs, wb_host, wb_input,
> wb_interp, wb_json, wb_lua_host, wb_mode, wb_objective, wb_prof, wb_render,
> wb_script, wb_snap, wb_space_bind, wb_spawn, wb_util, wb_worldspace`

Everything else — `wb_gpu*`, `wb_physics*`, `wb_assets*`,
`wb_avatar*`, `wb_spatial`, `wb_tiles`, `src/net/*`, `wb_edit`, `wb_heightfield`,
all 15 of the nesting/geo layer — is **not reachable** from the host. It is
linked into the binary and never called.

The number moves as composition proceeds: 11 before Phase 1 began, 15 when the
determinism cluster was wired in, 18 when input/camera/character joined, 19 when
the renderer joined. Run `tools/wiring_report.py` for the current value.

### 2.3 Dead and duplicated

- **`wb_exp` is unreachable, and it is load-bearing for a shipped pad.** The
  "experience container" is referenced by nothing outside `wb_exp.c` and its
  test — instance management moved to `wb_dist`. But it is also the *only*
  consumer of `wb_snap`, `wb_interp`, `wb_prof` and (through `wb_snap`) `wb_det`:
  the whole LP-03/LP-04 determinism, snapshot and profiling cluster hangs off it.
  Deleting it would orphan a shipped pad's implementation. The resolution is to
  make it the live path (Phase 1), not to remove it.
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

### 3.4 What makes the loop cohere, and how it breaks

The research pass confirms the coupling and names the conditions. The flywheel
coheres when **player and creator are the same account on one economy and one
discovery surface**; it breaks when money, tools, moderation or distribution are
built for one side and bolted onto the other.

**What makes it cohere**

1. **One identity, one currency, one client.** Fortnite pays engagement payouts
   from 40% of Item Shop net revenue, in real money, with no pseudo-economy
   underneath; Roblox's DevEx is the analogue. Neither asks a player to become a
   different kind of user.
2. **Tools inside the loop; power over accessibility.** Templates and remixing
   attack the blank-canvas cliff by turning consumption into creation. Rec Room
   learned the opposite — its consumer-grade tools "frustrated our more impactful
   creators", while real creation came almost entirely from PC and VR.
3. **A discovery budget, not just a ranking.** Pure exploitation locks the
   catalogue; pure exploration wrecks the player experience. Roblox ranks on
   per-user signals with no single signal dominating and a low minimum-play
   threshold for new games.
4. **Money designed with community norms.** Minecraft Marketplace pays ~70% and
   has paid $500M+; CurseForge authors keep 50%. The distribution is top-heavy
   everywhere — median DevEx $1,575 in 2024 against a top-1,000 average of
   $820,000 — so the *median* creator's path must be designed deliberately.
5. **Moderation before scale, and creators are not unpaid moderators.**
   Moderation cost scales with published items and audience reach, not revenue,
   so it grows before the money does.

**Documented ways it breaks**

- **Retrofitting money into a gift economy.** Valve's 2015 paid mods (25% to
  creators) were withdrawn four days later after a 133,000-signature petition.
- **Cold-start asymmetry.** Core reached 3.5M users in 18 months but "did very
  well with the creators… with the players, I would say it was a little harder."
- **Scope blowup across devices.** Rec Room cut half its staff and shut down:
  "too small to realize the 'anyone can build anywhere' vision, but too big to
  pivot."
- **No killer task.** Second Life had a complete creator economy — $567M peak
  volume, a convertible currency, IP ownership — and peaked at ~88,200 concurrent
  users, because "what am I supposed to do here?" had no answer.
- **Formula churn.** Fortnite changed its payout formula twice in 14 months, each
  shift reallocating the pool.

---

## 4. Gap analysis

### 4.1 Composition — the host does not reach the engine

The single largest gap. The shipped binary touches 11 modules; 81 are off-path.
This is not a criticism of the modules — most are genuinely good — it is the
difference between a library and a product:

- **No platform layer — now closed.** `src/` held only `core`, `lua`, `lv` and
  `net`: there was no window, no framebuffer presentation and no input source
  anywhere, so the engine could render a frame and had nowhere to put it.
  `src/platform/wb_win32.{h,c}` is that layer — a window, GDI presentation, and
  OS events into `wb_input`.
- **No render path — now closed.** `wb_render` (software rasteriser) was a leaf;
  the host never drew a frame. The host now renders every mode into a framebuffer:
  `wb_host_render` clears to sky, then draws a marker at every live body through
  the follow camera, with the player brighter and larger. `--render out.ppm`
  writes the frame so a headless run can show what it drew, and `make slice`
  asserts the centre pixel is the player and not the sky. `wb_gpu*` (the D3D12
  RHI) is still off the path: the software rasteriser is the portable floor and
  this is it in use.
- **No physical path.** `wb_physics*` (MuJoCo world/body/bind) are leaves.
- **No spatial path.** `wb_spatial` and `wb_tiles` (the quadkey spine and the
  resident tile store) are leaves from the host. The world has no terrain and no
  streaming in the shipped path.
- **No avatar path.** `wb_avatar*` are leaves; there is no body to inhabit.
- **No net path.** `src/net/*` and `wb_netmodel`/`wb_handoff`/`wb_match` are
  leaves; the shipped experience is single-process and local.
- **No input path — now closed.** `wb_input`, `wb_camera` and `wb_character` were
  leaves. The host owns all three: input is translated to actions through a
  remappable key map, the controller drives the player's velocity over the ECS,
  and the camera follows. Input drives the body only when a movement action is
  held, so a mode's own script still owns the player when no key is down. The
  MuJoCo adapter was split out (`wb_character_mj.{h,c}`) so the controller is
  backend-neutral and no MuJoCo type is in the host's include graph at all.
- **No sim-timing path — now closed.** The fixed-timestep accumulator, the
  per-tick divergence digest and the profiler all hung off `wb_exp`, which the
  host never calls. The host now owns all three: it steps at a fixed rate, refreshes
  the digest every tick, and profiles every frame with a budget on the frame zone
  (see Phase 1). The whole LP-01/LP-03/LP-04/LP-05 cluster is on the shipped path.

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

### 5.4 Where these mechanisms come from

Each mechanism above is standard practice somewhere; none of this is invented.
The research pass is in §8.

| Our mechanism | The practice it comes from |
|---|---|
| **M1** vertical-slice gate | **Unreal Gauntlet** (session runner for smoke/perf/stress across platforms); **Unreal Functional Testing** (scripted gameplay assertions with ground-truth comparison); **Unreal Automation Spec** (BDD); **Unity Test Framework + Performance Testing**; **Unity Game Simulation** (cloud bot playthroughs with a parameter grid) |
| **M2** experience gates on a place | **Roblox UGC validation** (triangle budgets, bounding boxes, watertightness, per-type rules, fixable errors); **Second Life Mesh Asset Validation**; **VRChat SDK Validations + Avatar Performance Ranking** (rank gates visibility on standalone/mobile); **osu! ranking procedure**; **Minecraft Marketplace** two-stage review |
| **M3** bot playtests | **Procedural Personas** (MCTS with evolved heuristics as synthetic playtesters); **curiosity-driven RL** (EA SEED — novelty-rewarded agents that expose unreachable content); **Generative Personas** (Go-Explore imitating 100+ players); **Rare's TDD for AI** (100+ internal deployments before launch) |
| **M4** creator-workflow gate | **Roblox Script Analysis / Luau** (static analysis + gradual types in the editor); **Roblox publish/update moderation**; **mod.io four-level moderation** (scan → rules → approval → reports); **Roblox Open Cloud Place Publishing API** — asynchronous upload with an Operation ID, explicitly designed to be called from a GitHub Action after integration tests |
| **M5** experience telemetry | **EA Analytics** (150 GB/day per 1M DAU); **EA DICE's 3D spatial telemetry viewer** (where players cluster, die, quit); **telemetry heatmaps** over level geometry; **PlayFab Experiments** (A/B with Sample Ratio Mismatch detection); **Hodent's hypothesis-first rule** — telemetry answers WHAT, not WHY, so instrument against a stated hypothesis |
| **M6** experience heuristics | **HEP — Heuristic Evaluation for Playability** (CHI 2004); **Games User Research** methods (observation, think-aloud, triangulation); **Behavioral Telemetry in Games User Research** (Springer 2015) |
| **M7** flywheel gate | **Roblox playtest modes + playtest subagent** (returns Pass/Fail/Inconclusive/Error per scenario); **Fortnite Project Analytics + engagement payout** (the payout formula consumes active playtime and island retention — creator feedback derived from player behaviour) |

Two more practices belong in the plan even though they are not CI gates:

- **Structured human playtests.** Valve's Cabal (silent observation, cross-disciplinary,
  iterate), Riot's per-sprint playtest, CDPR's RED Program (eye-tracking + telemetry
  + observation triangulation), DICE Battlefield Labs, EA Player Experience Labs.
  A bot cannot tell you a place is boring.
- **Platform playtest infrastructure.** Xbox Playtest, PlayStation Beta, Steam
  Playtest, Xbox Research + Parsec (9,000+ gamers/year), Minecraft's weekly
  snapshots. This is how the first audience is found before there is a discovery
  algorithm.

---

## 6. Prioritised roadmap

Each phase has an exit criterion that is a *running program*, not a document.

### Phase 0 — Make the binary honest *(in progress)*
- Disambiguate the two quadkey APIs so `make all` links — **done, `d5773c9`**.
- Ship the vertical-slice gate — **done, `tools/slice.sh` and `make slice`**. It
  builds, refuses a stale binary, plays both shipped gametypes and asserts the
  output. Verified that it fails when a source is touched and passes after a
  rebuild, so it is a real gate and not theatre.
- Resolve `wb_exp` — **not by deleting it.** It looks dead (the host never calls
  it) but it is the only consumer of `wb_snap`, `wb_interp`, `wb_prof` and
  `wb_det`, i.e. the whole LP-03/LP-04 determinism cluster; removing it would
  orphan a shipped pad. The resolution is to make it the live path, which is the
  first job of Phase 1.
- **Exit:** `make all` links, `make slice` passes, and a stale-run check fails CI.

### Phase 1 — Composition: put the engine in the host *(in progress)*
Wire, in order, each currently-unreachable subsystem into the shipped path:
**render → input/camera/character → spatial/tiles → physics → assets/avatar**.
This is largely inverting the existing leaf modules, not writing new ones.

Done so far:
- **The host owns the fixed-step accumulator and the divergence digest.** The
  shipped loop used to step at a hard-coded `1/60` with no accumulator and no
  digest; it now advances `wb_det_fixed`, integrates physics once per whole step,
  and refreshes `wb_snap_digest_world` over position + velocity every tick. The
  CLI reports `ticks` and the digest, and `make slice` asserts that two identical
  runs produce the same digest — LP-03's contract, now checked on the shipped
  path.
- **The host owns the frame profiler.** Every step is inside a `frame` zone, with
  `script`, `control` and `physics` zones nested in it, and the frame zone carries
  the LP-05 budget. The CLI reports the frame mean/p95/max and the overrun count.
- **The host owns input, the camera and the player controller.** Input is mapped
  to actions through a remappable key map; the controller drives the player's
  velocity over the ECS; the camera follows. `make slice` now drives the path
  with a held action and asserts the player moves under input and stands still
  without it. `wb_character_mj.{h,c}` is the MuJoCo adapter, split out so the
  controller is backend-neutral and the host's include graph contains no MuJoCo
  type at all.
- **The host renders the world.** `wb_host_render` draws every live body as a
  marker through the follow camera — the shipped world is an ECS of bodies with
  no terrain, so the entity layer is what "a frame of the world" means. The
  renderer gained `wb_render_marker` for it, covered by six new checks in the
  render gate. `--render out.ppm` writes the frame, and `make slice` asserts the
  centre pixel is the player. Reachability moved from 11 to 19 of 93. The
  platform layer (`src/platform/wb_win32.{h,c}`) then took it to 20 of 94: a real
  window, GDI presentation of an XRGB8888 DIB section, and OS events into the
  host's `wb_input`, so `--window` opens a window a person can drive.

- **The platform layer exists and is wired in.** `src/platform/wb_win32.{h,c}` is
  the first file in the repo that knows what an OS is: a window whose client area
  is exactly the framebuffer, GDI presentation of an XRGB8888 DIB section, and
  keyboard/mouse events fed straight into the host's `wb_input`. `--window` opens
  it and renders every frame into it; `--frames N` makes it self-terminating so
  it can be smoke-tested. A real person at a keyboard now drives the same
  input → controller → velocity path that `--input` drives headlessly.
  `test-window` covers it: 16 checks over the HWND, the framebuffer, the message
  → input path and the close path.

- **Exit:** met for the shipped path. `make play` opens a window, draws a frame
  of the world into it, and the player moves under keyboard input. No new
  gameplay. What is still missing is a *game*: there is nothing to do in the
  world yet.

### Phase 2 — The player loop
Add the missing stages: **objective** (win/lose/score/timer), **onboarding**,
**animation and audio** (all of P2, currently 0/10), **UI/HUD** (`LP-42`).

**The objective is shipped (LP-114).** `wb_objective` is a deterministic match
clock: a score per side, a clock that advances only when the simulation does,
and a result of won / lost / drawn. Three rules — first-to, timed (highest score
at the limit, level is a draw), and survive (sides with no deaths at the limit).
It refuses nonsense: an unknown rule, a one-sided objective, a zero target, a zero
time limit on a timed rule, an out-of-range side.

The host owns it, not the script. `duel-stadium` declares its objective in
`mode.json` through the config schema; `wb_host_open` installs it; `wb_host_step`
advances it by exactly one fixed step per sim tick; and `wb_host_running` returns
false once it resolves. The mode's script neither counts ticks nor holds a score
— it plays toward the objective through `wb.objective_score(side, points)` and
reads the clock back. The script's tuning is data too, read through `wb.cfg`.

`duel-stadium` is now a real loop: an opponent body walks at the player and tags
on contact, first to three takes it. With no input the bot wins 0-3 in 176 ticks;
with `--input fwd` the player runs, the bot never catches up, and the match is a
0-0 draw at the limit. Both are a completed loop with an objective, which is the
phase's exit criterion. `make slice` asserts the resolution and the bot's scoring
line, and `test-objective` covers the rules and refusals.

- **Exit:** met. A bot completes a full loop with an objective, on the shipped
  path, with a deterministic digest (`duel-stadium` = `f11c863a100e5101`).
  What remains in this phase is the rest of the loop: **onboarding** (a first-time
  player is told what to do), **animation and audio** (nothing moves or sounds
  yet — the marker is a billboard), and **UI/HUD** (`LP-42`: the score exists but
  nothing on screen shows it).

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
6. **The flywheel has documented ways to fail, and they are not hypothetical.**
   Valve withdrew paid mods in four days; Core's creators came and its players
   did not; Rec Room broke on "anyone, anywhere"; Second Life built a complete
   creator economy and still had no answer to "what am I supposed to do here?".
   The specific risk for WWW is **cold-start asymmetry** — it is far easier to
   build creator tools than to put a player in front of them — and **no killer
   task**, which is a design problem, not an engineering one.
7. **Money is a design decision, not a feature.** Every platform that got it
   wrong did so by bolting a formula on afterwards (Valve 2015; Fortnite's two
   formula changes in 14 months). If WWW ever adds a creator economy, the split,
   the currency and the discovery budget must be designed together, once.

---

## 8. Sources

Three sources, in order of authority for this document: the measurements in §2
(reproducible with `tools/wiring_report.py`), the project's own citation ledger
(`docs/LILYPADS.md`, 631 sources), and a dedicated research pass commissioned for
this assessment — three cited briefs:

- *Validating Player Experience as an Engineering Discipline* (718 words, 60+
  sources distilled)
- *Creator validation & workflow mechanisms* (852 words, 34 sources)
- *The Player–Creator Flywheel: What Makes It Cohere or Break* (898 words, 23
  sources, 67% sentence coverage)

**Player-experience and engine practice**

- **Unreal Gauntlet** — session runner for smoke/perf/stress across platforms.
  https://docs.unrealengine.com/5.0/en-US/gauntlet-automation-framework-in-unreal-engine/
- **Unreal Functional Testing** — scripted gameplay assertions with ground truth.
  https://dev.epicgames.com/documentation/en-us/unreal-engine/functional-testing-in-unreal-engine
- **Unreal Automation Spec (BDD)**.
  https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-spec-in-unreal-engine
- **Unity Test Framework + Performance Testing**; **Unity Game Simulation**.
  https://unity.com/how-to/automated-tests-unity-test-framework ·
  https://docs.unity3d.com/Packages/com.unity.simulation.games@0.4/manual/index.html
- **Procedural Personas** — MCTS synthetic playtesters.
  https://arxiv.org/abs/1802.06881
- **Curiosity-driven RL** (EA SEED) — novelty-rewarded coverage agents.
  https://dl.acm.org/doi/abs/10.1109/CoG52621.2021.9619048
- **Generative Personas** — Go-Explore imitating 100+ players.
  https://arxiv.org/abs/2209.00459
- **Rare's TDD for AI** — 100+ internal deployments before launch.
  https://www.gamedeveloper.com/design/how-rare-automates-testing-for-ai-and-more-in-sea-of-thieves-part-4-of-4-
- **HEP — Heuristic Evaluation for Playability** (CHI 2004).
  https://www.eng.auburn.edu/~sealscd/COMP7970/notes/Playtesting/usingheuristics.pdf
- **Behavioral Telemetry in Games User Research** (Springer 2015).
  https://doi.org/10.1007/978-3-319-15985-0_7
- **Celia Hodent** — UX practices at Epic; the hypothesis-first rule.
  https://celiahodent.com/ux-practices-epic-games/
- **EA Analytics** — 150 GB/day per 1M DAU.
  https://www.ea.com/ea-studios/ea-romania/news/the-stories-told-by-numbers-peek-inside-the-ea-analytics-department
- **PlayFab Experiments** — A/B with Sample Ratio Mismatch detection.
  https://docs.xbox.com/services/playfab/live-service-management/game-configuration/experiments
- **Valve's Cabal**; **Riot's per-sprint playtest**; **CDPR's RED Program**;
  **DICE Battlefield Labs**; **EA Player Experience Labs** — the human playtest
  programmes. https://arstechnica.com/gaming/2024/11/how-valve-made-half-life-2-and-set-a-new-standard-for-future-games/
- **Xbox Research + Parsec** — 9,000+ gamers/year of virtual playtesting.
  https://developer.microsoft.com/en-us/games/articles/2023/05/how-xbox-research-accomplished-worldwide-virtual-playtesting-with-parsec/
- [145] GDC — *Overwatch* Gameplay Architecture and Netcode ·
  [73] GDC — Dedicated Servers in Gears of War 3 ·
  [147][150][155][306] Tracy / Optick / statistics-ring practice

**Creator workflow and validation**

- **Roblox UGC validation system** — pre-upload geometry/texture rule checks with
  fixable errors. https://create.roblox.com/docs/marketplace/validation-system
- **Roblox Script Analysis / Luau** — static analysis and gradual types in-editor.
  https://create.roblox.com/docs/studio/script-editor ·
  https://create.roblox.com/docs/en-us/luau/type-checking
- **Roblox content moderation** — publish and re-upload review.
  https://en.help.roblox.com/hc/en-us/articles/21416271342868-Content-Moderation-on-Roblox
- **Roblox Open Cloud — Place Publishing API**, built for CI/CD.
  https://create.roblox.com/docs/cloud/guides/usage-place-publishing.md
- **Roblox playtest modes + playtest subagent** (Pass/Fail/Inconclusive/Error).
  https://create.roblox.com/docs/en-us/studio/testing-modes.md
- **Roblox Analytics Dashboard + Query API**; **Roblox Experiments**.
  https://create.roblox.com/docs/production/analytics/analytics-dashboard ·
  https://create.roblox.com/docs/production/experiments
- **mod.io four-level moderation**. https://docs.mod.io/moderation
- **Second Life Mesh Asset Validation**.
  https://community.secondlife.com/knowledgebase/english/uploading-a-mesh-model-r274
- **VRChat SDK Validations + Avatar Performance Ranking**.
  https://creators.vrchat.com/avatars/avatar-performance-ranking-system
- **osu! ranking procedure**. https://osu.ppy.sh/wiki/en/Beatmap_ranking_procedure
- **Fortnite island publishing** and **Project Analytics / engagement payout**.
  https://dev.epicgames.com/documentation/en-us/fortnite/publishing-islands-in-fortnite-creative ·
  https://dev.epicgames.com/documentation/fortnite/project-analytics-for-fortnite-games
- **Minecraft Partner Center / Marketplace review**.
  https://www.minecraft.net/en-us/partner
- **Steam Workshop moderation API** (change numbers so approvals cannot apply to
  unreviewed updates). https://partner.steamgames.com/doc/webapi/IPublishedFileModerationService
- [490] marcmicalizzi/game_engine — asset pipeline, **provenance, licence
  allowlist** · [492] `assetpipe` — provenance, dependency graphs, **validation** ·
  [488] Defold hot reload · [481] Unity asset import pipeline ·
  [483] Unreal multi-user editing · [487] Unreal Horde

**The flywheel**

- **Roblox Annual Economic Impact Report** — $1B+ paid out, +31% YoY; median DevEx
  $1,575 vs top-1,000 average $820k.
  https://about.roblox.com/newsroom/2025/09/roblox-annual-economic-impact-report
- **Fortnite Creator Economy 2.0** — payouts from 40% of Item Shop net revenue.
  https://www.fortnite.com/news/introducing-the-creator-economy-2-0
- **Fortnite ecosystem year in review** — $352M / 70,000 creators / 198,000
  islands / 36.5% of playtime.
  https://www.fortnite.com/news/fortnite-ecosystem-2024-year-in-review-celebrating-creators-and-looking-ahead
- **"Every Game a Platform"** — the UGC gold rush, its pitfalls and the
  cold-start asymmetry. https://www.gamesindustry.biz/every-game-a-platform-pitfalls-and-opportunities-in-the-gold-rush-for-user-generated-content
- **Rec Room layoffs and shutdown** — "anyone, anywhere" as the failure mode.
  https://www.uploadvr.com/rec-room-half-staff-layoffs ·
  https://www.gamedeveloper.com/business/rec-room-shutting-down-after-failing-to-become-sustainability-profitable-
- **Valve paid mods, withdrawn in four days**.
  https://www.theverge.com/2015/4/27/8505899/valve-steam-paid-mods-removed-canceled
- **Second Life's dead end** — $567M peak volume, ~88,200 peak CCU, no killer task.
  https://www.geschichte-der-informatik.de/articles/dead_end_second_life
- **Minecraft Marketplace** — ~70% split, $500M+ paid.
  https://corq.studio/insights/minecraft-marketplace-inside-the-platforms-500-million-creator-economy
- **Roblox discovery FAQ** — per-user ranking signals and reduced-exposure rules.
  https://github.com/Roblox/creator-docs/blob/main/content/en-us/discovery-faq.md
- [586][587][588] Roblox teleport / publishing / streaming ·
  [591] Minecraft World persistence · [596] EVE Tranquility Tech IV ·
  [598] Warcraft Instance · [599] Second Life Land ·
  [167] VRChat Safety and Trust System · [618] OGC AI-DGGS Pilot

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
mingw32-make test            # 90 gate results / 8,544 assertions / 0 fail

# the shipped path
mingw32-make all && ./build/wubu_engine.exe games omniverse-hub
mingw32-make slice           # asserts the above, and refuses a stale binary

# module wiring / reachability
python3 tools/wiring_report.py        # leaf vs wired vs unwired, and reachability
```
