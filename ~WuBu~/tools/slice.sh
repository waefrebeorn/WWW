#!/usr/bin/env bash
# Vertical-slice gate -- docs/EXPERIENCE-ASSESSMENT.md, mechanism M1.
#
# The module gates prove each capability works in isolation. This checks the one
# thing they cannot: that the SHIPPED PATH still works end to end. It builds the
# engine, refuses to run a stale binary, then plays the shipped gametypes and
# asserts what came out.
#
# It is deliberately tiny. A vertical slice is not a test suite -- it is the
# smallest thing that can fail when the product breaks. Everything richer
# (experience gates on a place, bot playtests, telemetry) is built on top of this
# script's result, not instead of it.
#
# Usage:  tools/slice.sh        (or `mingw32-make slice`)

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

FAIL=0
note() { printf '  %s\n' "$*"; }
ok()   { printf '  ok  %s\n' "$*"; }
bad()  { printf '  FAIL %s\n' "$*"; FAIL=1; }

# ---- 1. build -------------------------------------------------------------
mkdir -p build
if [ ! -x build/wubu_engine.exe ]; then
  note "build/wubu_engine.exe missing -- building"
  mingw32-make all >/dev/null 2>&1 || { bad "mingw32-make all failed"; exit 1; }
fi

# ---- 2. refuse a stale binary --------------------------------------------
# A run against a binary older than the sources proves nothing, and a green run
# over a stale build is the easiest false confidence there is.
STALE="$(find src -type f \( -name '*.c' -o -name '*.h' \) -newer build/wubu_engine.exe 2>/dev/null)"
if [ -n "$STALE" ]; then
  bad "stale binary -- rebuild with 'mingw32-make all':"
  for f in $STALE; do note "$f"; done
  exit 1
fi

# ---- 3. play the shipped path and assert ----------------------------------
# `want` strings are matched with grep -F: they are the observable contract of
# the slice. If a gametype stops producing one of them, the slice is broken even
# if the process still exits 0.
run_case() {
  local dir="$1"; shift
  local out rc
  out="$(./build/wubu_engine.exe games "$dir" 2>&1)"
  rc=$?

  if [ "$rc" -ne 0 ]; then
    bad "$dir: exited $rc"
    printf '%s\n' "$out" | sed 's/^/       /'
    return
  fi
  if [ -z "$out" ]; then
    bad "$dir: produced no output"
    return
  fi
  local want
  for want in "$@"; do
    if ! printf '%s\n' "$out" | grep -qF -- "$want"; then
      bad "$dir: missing '$want'"
      printf '%s\n' "$out" | sed 's/^/       /'
      return
    fi
  done
  ok "$dir"
}

note "vertical slice"
run_case "omniverse-hub" \
  "in 'omniverse-hub' as player-1" \
  "stepped through rift 0 into instance" \
  "engine exited clean"

run_case "duel-stadium" \
  "in 'duel-stadium' as player-1" \
  "engine exited clean" \
  "objective won, score 0-3"

# ---- 4. determinism -------------------------------------------------------
# LP-03's contract, checked on the shipped path: identical runs must produce an
# identical digest. A differing digest is a desync, whatever the cause -- a
# different step count, an uninitialised read, a hash-order dependence.
det_case() {
  local dir="$1" d1 d2
  d1="$(./build/wubu_engine.exe games "$dir" 2>&1 | grep -o 'digest [0-9a-f]*')"
  d2="$(./build/wubu_engine.exe games "$dir" 2>&1 | grep -o 'digest [0-9a-f]*')"
  if [ -z "$d1" ]; then
    bad "$dir: no digest emitted"
  elif [ "$d1" != "$d2" ]; then
    bad "$dir: digest differs between identical runs ($d1 vs $d2)"
  else
    ok "$dir: deterministic ($d1)"
  fi
}

note "determinism"
det_case "omniverse-hub"
det_case "duel-stadium"

# ---- 5. input path --------------------------------------------------------
# The player must move when input says so and stand still when it does not. This
# is the path a windowed platform layer drives with real keys; the headless
# binary drives it with a held action.
note "input"
move_case() {
  local action="$1" expect="$2" pos
  pos="$(./build/wubu_engine.exe games duel-stadium player-1 --input "$action" 2>&1 \
        | sed -n 's/^wb: player-1 at (\(.*\))$/\1/p')"
  if [ "$expect" = "moved" ]; then
    case "$pos" in
      ""|"0.00, 0.00, 0.00") bad "$action: player did not move (at '${pos:-nowhere}')" ;;
      *) ok "$action: player moved to ($pos)" ;;
    esac
  else
    if [ "$pos" = "0.00, 0.00, 0.00" ]; then
      ok "$action: player stayed put"
    else
      bad "$action: player moved to ($pos), expected it to stay put"
    fi
  fi
}

move_case fwd moved
move_case back moved
move_case left moved
move_case none still

# ---- 5b. objective --------------------------------------------------------
# The duel must complete a loop with a result: a bot plays it to a score, the
# objective resolves, and the host ends the match on the resolution rather than
# on a tick count. This is the player-loop criterion from the assessment.
note "objective"
obj_out="$(./build/wubu_engine.exe games duel-stadium 2>&1)"
if printf '%s\n' "$obj_out" | grep -qF "objective won, score 0-3"; then
  ok "duel: a full loop completed with a result (0-3)"
else
  bad "duel: the match did not resolve"
  printf '%s\n' "$obj_out" | sed 's/^/       /'
fi
if printf '%s\n' "$obj_out" | grep -qF "bot tags player-1"; then
  ok "duel: the bot scored through the script's objective binding"
else
  bad "duel: the bot never scored"
fi

# ---- 6. render ------------------------------------------------------------
# A frame of the world must be renderable, and it must contain the player: the
# centre pixel of the saved frame is the player's marker, not the sky.
note "render"
ppm="build/frame.ppm"
rm -f "$ppm"
render_out="$(./build/wubu_engine.exe games duel-stadium player-1 --input fwd \
                                              --render "$ppm" 2>&1)"
echo "$render_out" | grep -q "rendered 1 marker(s)" \
  || bad "render: no marker reported"
[ -s "$ppm" ] || bad "render: no frame written"
# The PPM header is "P6\n640 480\n255\n" = 15 bytes; the centre pixel of a
# 640x480 frame sits at 15 + (240*640 + 320) * 3.
off=$((15 + (240 * 640 + 320) * 3))
centre="$(tail -c +$((off + 1)) "$ppm" | head -c 3 | od -An -tx1 | tr -d ' \n')"
if [ "$centre" = "1a2430" ]; then
  bad "render: centre pixel is sky -- the player was not drawn"
else
  ok "render: player drawn at frame centre (pixel $centre)"
fi

# The HUD is a screen-space layer drawn after the markers: the match's state,
# score and clock must be VISIBLE, not merely printed to stdout. The panel is
# drawn at (12,12); the first lit pixel of the "S" in STATE is at (14,12).
hud_off=$((15 + (12 * 640 + 14) * 3))
hud_px="$(tail -c +$((hud_off + 1)) "$ppm" | head -c 3 | od -An -tx1 | tr -d ' \n')"
if [ "$hud_px" = "1a2430" ]; then
  bad "hud: the match panel was not drawn (pixel is sky)"
else
  ok "hud: match panel drawn at the top-left (pixel $hud_px)"
fi

# Onboarding: a first-time player is told what to do. The hint is up for the
# first `hint_seconds` of the match, so this frame is rendered at tick 60 -- a
# full-length run would have retired it.
hint_ppm="build/hint.ppm"
rm -f "$hint_ppm"
hint_out="$(./build/wubu_engine.exe games duel-stadium --frames 60 --render "$hint_ppm" 2>&1)"
echo "$hint_out" | grep -q "rendered" || bad "hint: no frame written"
# "WASD TO MOVE - SPACE TO JUMP - SHIFT TO SPRINT" is 46 glyphs: 46*6-1 = 275 wide,
# so the text starts at (640-275)/2 = 182 and the plate at 182-4 = 178; the text
# sits at 480 - 7 - 12 = 461 and the plate at 461-3 = 458.
hint_off=$((15 + (458 * 640 + 178) * 3))
hint_px="$(tail -c +$((hint_off + 1)) "$hint_ppm" | head -c 3 | od -An -tx1 | tr -d ' \n')"
if [ "$hint_px" = "1a2430" ]; then
  bad "hint: nothing was drawn for onboarding (pixel is sky)"
else
  ok "hint: onboarding line drawn at the bottom (pixel $hint_px)"
fi

# ---- 7. verdict -----------------------------------------------------------
if [ "$FAIL" -ne 0 ]; then
  printf '\nSLICE FAILED\n'
  exit 1
fi
printf '\nSLICE OK\n'
