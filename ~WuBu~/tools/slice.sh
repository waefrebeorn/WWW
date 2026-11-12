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

# Count the pixels in a box of a saved P6 frame that are NOT the sky colour.
# The frame is 640x480 and the header is 15 bytes ("P6\n640 480\n255\n"), so a
# pixel's bytes sit at 15 + (y*640 + x)*3. A box is read a row at a time and the
# hex is searched for the sky triplet; the figure's colours share no adjacent
# byte pair with it, so the count cannot be fooled by a pixel boundary.
lit_in_box() {  # file x0 y0 x1 y1 -> count of non-sky pixels
  local f=$1 x0=$2 y0=$3 x1=$4 y1=$5 y off len hex sky total
  hex=""
  for (( y=y0; y<y1; y++ )); do
    off=$(( 15 + (y * 640 + x0) * 3 ))
    len=$(( (x1 - x0) * 3 ))
    hex="$hex$(tail -c +$((off + 1)) "$f" | head -c "$len" \
                | od -An -tx1 -v | tr -d ' \n')"
  done
  total=$(( (x1 - x0) * (y1 - y0) ))
  sky=$(printf '%s' "$hex" | grep -o '1a2430' | wc -l)
  echo $(( total - sky ))
}

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
# A frame of the world must be renderable, and it must contain the player. The
# player is a FIGURE now rather than a disc, so the check is a box around the
# frame centre rather than one pixel: the exact centre of a standing figure is
# sky between its legs.
note "render"
ppm="build/frame.ppm"
rm -f "$ppm"
render_out="$(./build/wubu_engine.exe games duel-stadium player-1 --input fwd \
                                              --render "$ppm" 2>&1)"
echo "$render_out" | grep -qE "rendered [1-9][0-9]* marker\(s\)" \
  || bad "render: no body reported"
[ -s "$ppm" ] || bad "render: no frame written"
# The player is drawn at the frame centre; a 60x60 box there must be mostly
# figure, and must not be empty.
centre_lit=$(lit_in_box "$ppm" 290 210 350 270)
if [ "$centre_lit" -lt 20 ]; then
  bad "render: the frame centre holds no figure ($centre_lit lit pixels)"
else
  ok "render: the player's figure is drawn at the frame centre ($centre_lit px)"
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

# ---- 6. animation ---------------------------------------------------------
# The bodies are figures now, not discs, and the stride is driven by each body's
# own velocity. Two frames of a DRIVEN player therefore differ far more than two
# frames of a player left alone, where nothing but residual velocity moves.
#
# The compared region is a full-width band across the figure, so it is one
# contiguous run of the PPM and `cmp` can diff it directly -- no pixel parsing.
anim_band() {  # file y0 y1 -> raw bytes of the full-width rows [y0,y1)
  tail -c +$(( ($2 * 640) * 3 + 16 )) "$1" | head -c $(( ($3 - $2) * 640 * 3 ))
}
note "animation"
for f in 120 131; do
  ./build/wubu_engine.exe games duel-stadium --frames $f --input fwd \
      --render "build/anim_fwd_$f.ppm" >/dev/null 2>&1
  ./build/wubu_engine.exe games duel-stadium --frames $f --input none \
      --render "build/anim_none_$f.ppm" >/dev/null 2>&1
done
anim_fwd=$(cmp -l <(anim_band build/anim_fwd_120.ppm 200 300) \
                  <(anim_band build/anim_fwd_131.ppm 200 300) | wc -l)
anim_none=$(cmp -l <(anim_band build/anim_none_120.ppm 200 300) \
                   <(anim_band build/anim_none_131.ppm 200 300) | wc -l)
if [ "$anim_fwd" -gt 200 ] && [ "$anim_fwd" -gt $((anim_none * 2)) ]; then
  ok "anim: a driven player's figure changes ($anim_fwd bytes vs $anim_none undriven)"
else
  bad "anim: the figure barely changes under motion (driven $anim_fwd, undriven $anim_none)"
fi

# ---- 7. audio -------------------------------------------------------------
# The match makes a sound: a footfall per half stride while the player moves, a
# tone when the score changes. The engine writes it as a WAV -- the same rule as
# the PPM frames, a file the space owns rather than a device it drives.
note "audio"
snd_wav="build/match.wav"
rm -f "$snd_wav"
snd_out="$(./build/wubu_engine.exe games duel-stadium --frames 600 --input fwd \
                                    --sound "$snd_wav" 2>&1)"
echo "$snd_out" | grep -q "wrote" || bad "audio: nothing was written"
[ -s "$snd_wav" ] || bad "audio: no file"
# 600 frames at the 60 Hz fixed step is 10 s; at 24 kHz that is 240000 frames,
# and a WAV carries a 44-byte header, so the file must be exactly 480044 bytes.
# The size is the clock check: it proves the audio ran on SIMULATION time.
snd_size=$(wc -c < "$snd_wav")
if [ "$snd_size" -ne 480044 ]; then
  bad "audio: wrong size ($snd_size bytes, expected 480044)"
else
  ok "audio: the mix is 10 s at 24 kHz ($snd_size bytes)"
fi
# Silence is all zero bytes; anything else is a sound. `tr -d '0'` strips the
# zero nibbles, so a silent mix leaves nothing. The WHOLE file is scanned: the
# first footstep is a sixth of a second in, so a prefix would miss it.
snd_hex="$(tail -c +45 "$snd_wav" | od -An -tx1 -v | tr -d ' \n')"
snd_nonzero=$(printf '%s' "$snd_hex" | tr -d '0' | wc -c)
if [ "$snd_nonzero" -lt 100 ]; then
  bad "audio: the mix is silent"
else
  ok "audio: the mix is audible ($snd_nonzero non-zero nibbles)"
fi
# The run must have produced events, not just a buffer.
echo "$snd_out" | grep -qE "\([1-9][0-9]* events," \
  || bad "audio: no sound events were emitted"
# And the engine must have measured its own output: the dominant frequency of the
# loudest window. A footfall is a low thud, so the peak must sit low -- this is
# the check that tells a footstep from a fault.
snd_peak="$(echo "$snd_out" | sed -n 's/.*peak \([0-9]\+\) Hz.*/\1/p' | head -1)"
if [ -z "$snd_peak" ]; then
  bad "audio: the engine did not report the mix's peak frequency"
elif [ "$snd_peak" -gt 200 ]; then
  bad "audio: the mix's peak is ${snd_peak} Hz -- a whine, not a footfall"
else
  ok "audio: the mix's peak is ${snd_peak} Hz (a low thud)"
fi

# ---- 8. verdict -----------------------------------------------------------
if [ "$FAIL" -ne 0 ]; then
  printf '\nSLICE FAILED\n'
  exit 1
fi
printf '\nSLICE OK\n'
