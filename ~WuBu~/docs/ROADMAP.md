# ROADMAP — the pond

A frog crosses a pond one hop at a time; every hop must land on a stone it can
stand on. The roadmap is the pond drawn out: the **sections** (swaths) you have
to cross, and the order the stones are laid so the next hop is always reachable.
It is not a schedule, and it is not a grid of phases. It is a walk.

The full stone list is `LILYPADS.md` (113 pads, dependency-ordered). This doc is
only the shape of the crossing. The whiteboard sections below are the working
draft — they are meant to be edited, not filed.

---

## The pond — eight sections

| Section | Stones (pads) | You're here when |
|---|---|---|
| **INSTRUMENT** | LP-01, 02, 05, 47, 48 | you can see a frame and a budget |
| **SIMULATE** | LP-03, 04, 06, 07, 08, 10 | the sim is reproducible and parallel |
| **RENDER** | LP-09, 11–21 | a dense world scales to film density |
| **ANIMATE** | LP-22, 23, 24, 27 | a character moves and aims |
| **EMIT** | LP-25, 26 | a world you can hear |
| **CONNECT** | LP-28–35 | multiplayer that holds up |
| **AUTHOR** | LP-36–45, 49 | a creator ships without C |
| **HOST** | LP-46, 50–59 | the world outlives a restart |

---

## How the crossing actually threads the sections

You don't finish a section and never come back. You hop into a section just far
enough to lay the stones another section needs, then move on. The walk, in
crossing order:

**1 · SEE IT — INSTRUMENT.**
Profiling, budgets, testing. You need this before you know anything about any
other section, and it never stops paying.

**2 · BUILD THE BEDROCK — SIMULATE.**
Determinism, fixed timestep, jobs, the access log, geometry optimization.
This section is paid for by two sections *later*: jobs unlock RENDER's heavy
stones, determinism unlocks CONNECT's fair stones. So you cross here early even
though the payoff is elsewhere.

**3 · MAKE IT LOUD — RENDER, then ANIMATE, EMIT alongside.**
RENDER first: bindless → materials → texture streaming → occlusion → LOD →
GPU-driven → mesh shaders. ANIMATE needs RENDER to be real before it is worth
anything. EMIT hops in step with it.

**4 · MAKE IT SHARED — CONNECT.**
Needs determinism + timestep from BEDROCK. delta → interpolation → lag comp →
rollback → meshing control plane → matchmaking.

**5 · LET OTHERS BUILD — AUTHOR, plus one early stone of HOST.**
The build pipeline (HOST/LP-46) lands *here*, not in the HOST section — the
moment there are two machines you need it. Then asset graph → shader graphs →
visual scripting → UI → terrain.

**6 · LET OTHERS STAY — HOST.**
persistence → identity → discovery → moderation → economy → interoperability.

---

## The forks — where hopping into one section early buys another

- **Determinism (SIMULATE) now, rollback (CONNECT) later.** You spend the
  determinism cost in BEDROCK and cash it in SHARED. Skipping it means CONNECT
  is rebuilt from scratch.
- **Jobs (SIMULATE) before GPU-driven + mesh shaders (RENDER).** Those two are
  compute-shaped and need a scheduler to mean anything.
- **The build pipeline (HOST/LP-46) early.** Not when you reach HOST. First
  machine that isn't yours.
- **Stub the platform concerns at the door they gate, not in HOST.**
  Matchmaking (CONNECT/LP-34) shows up with the first multiplayer game;
  accessibility (HOST/LP-59) with the first shipped gametype. Lay their minimum
  viable surface when the thing they gate ships, so nobody rebuilds the world to
  bolt them in later.

---

## The stones already underfoot

The engine runs a gametype package (slice 22). 52 gates, 6403 assertions, 0
fail. The quadkey spine, the render graph, content-addressed DDC, and declared
spawns in `mode.json` are not up for discussion in any of the above.

What not to break stays identical to `LILYPADS.md`. The sandbox escape is
accepted risk — recorded, not worked.
