#!/usr/bin/env bash
# fetch-mujoco.sh -- (re)fetch the vendored MuJoCo 4 release into thirdparty/mujoco/.
#
# MuJoCo is the engine's physics backend (slice 9). The Windows x86_64 release
# ships an MSVC-built mujoco.dll + import lib + headers; MinGW gcc links the
# import lib directly (verified on this box), so no MSVC toolchain is needed to
# build the engine.
#
# Usage:  bash tools/fetch-mujoco.sh [version]      (default 4.0.0)
set -euo pipefail

VER="${1:-4.0.0}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/thirdparty/mujoco"
URL="https://github.com/google-deepmind/mujoco/releases/download/${VER}/mujoco-${VER}-windows-x86_64.zip"

# Prefer a real python3; fall back to the known Windows install location.
PY="$(command -v python3 || true)"
if [ -z "$PY" ]; then
  for c in /c/Users/eman5/AppData/Local/Programs/Python/Python311/python.exe \
           /c/Python311/python.exe /c/Python312/python.exe; do
    [ -x "$c" ] && PY="$c" && break
  done
fi
if [ -z "$PY" ]; then
  echo "fetch-mujoco: need python3 to unpack the release (unzip mishandles its paths)" >&2
  exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "fetch-mujoco: $URL"
curl -sL "$URL" -o "$TMP/mujoco.zip"

mkdir -p "$DEST/include" "$DEST/lib" "$DEST/bin" "$DEST/model"
"$PY" - "$TMP/mujoco.zip" "$DEST" <<'PY'
import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
dest = sys.argv[2]
want = ("include/mujoco/", "lib/", "bin/mujoco.dll", "LICENSE")
for n in z.namelist():
    if n.startswith("model/"):
        # keep a small, useful model set for tests and demos
        if any(k in n for k in ("humanoid/", "cube/", "speculative/")):
            pass
        else:
            continue
    if not any(n.startswith(w) or n == w for w in want) and not n.startswith("model/"):
        continue
    out = os.path.join(dest, n.replace("/", os.sep))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(z.read(n))
print("fetch-mujoco: unpacked")
PY

echo "fetch-mujoco: done -> $DEST"
echo "  headers : $DEST/include/mujoco"
echo "  lib     : $DEST/lib/mujoco.lib"
echo "  dll     : $DEST/bin/mujoco.dll"
