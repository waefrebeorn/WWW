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
  "engine exited clean"

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

# ---- 5. verdict -----------------------------------------------------------
if [ "$FAIL" -ne 0 ]; then
  printf '\nSLICE FAILED\n'
  exit 1
fi
printf '\nSLICE OK\n'
