#!/usr/bin/env python3
"""Report how the engine's modules are actually wired.

Two questions, answered from the include graph rather than from documentation:

  1. WIRING. Is each module consumed by another module, or is it a leaf -- a
     capability with a green gate and no consumer? A leaf is not a defect (many
     are utilities), but a tree of leaves is a library rather than a product.

  2. REACHABILITY. Starting from the real entry points (the host binary and the
     Lua bridge), which modules does the shipped program actually touch? A module
     that is linked but unreachable is dead weight in the binary.

Usage:
    python3 tools/wiring_report.py [engine-root]

`engine-root` defaults to the ~WuBu~ directory containing this script's parent.
Requires only the Python standard library.
"""

import collections
import glob
import os
import re
import sys

ENTRY_POINTS = ["wb_host", "wb_worldspace", "wb_script", "wb_lua_host", "wb_space_bind"]


def find_engine_root():
    if len(sys.argv) > 1:
        return os.path.abspath(sys.argv[1])
    # tools/ lives directly under the engine root.
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def collect_files(root):
    files = []
    for sub in ("core", "lua", "net"):
        files += glob.glob(os.path.join(root, "src", sub, "**", "*.*"), recursive=True)
    return [f for f in files if f.endswith((".c", ".h"))]


def module_of(path):
    m = re.match(r"(wb_\w+)\.(c|h)$", os.path.basename(path))
    return m.group(1) if m else None


def build_graph(root, files):
    headers = {os.path.basename(h): os.path.basename(h)[:-2]
               for h in glob.glob(os.path.join(root, "src", "core", "wb_*.h"))}
    edges = collections.defaultdict(set)
    for f in files:
        src = module_of(f)
        if not src:
            continue
        try:
            with open(f, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            continue
        for inc in re.findall(r'#include\s+"(wb_[\w]+\.h)"', text):
            if inc in headers and headers[inc] != src:
                edges[src].add(headers[inc])
    return headers, edges


def reachable_from(entries, edges):
    seen, stack = set(), [e for e in entries if e in edges or e in {n for v in edges.values() for n in v}]
    while stack:
        cur = stack.pop()
        if cur in seen:
            continue
        seen.add(cur)
        stack.extend(n for n in edges.get(cur, ()) if n not in seen)
    return seen


def main():
    root = find_engine_root()
    files = collect_files(root)
    headers, edges = build_graph(root, files)
    modules = sorted(headers.values())

    includers = collections.defaultdict(set)
    for f in files:
        src = module_of(f)
        if not src:
            continue
        try:
            with open(f, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            continue
        for inc in re.findall(r'#include\s+"(wb_[\w]+\.h)"', text):
            if inc in headers:
                includers[headers[inc]].add(src)

    wired, leaf, unwired = [], [], []
    for mod in modules:
        consumers = includers.get(mod, set())
        if not consumers:
            unwired.append(mod)
        elif consumers == {mod}:
            leaf.append(mod)
        else:
            wired.append((mod, sorted(consumers)))

    reach = reachable_from(ENTRY_POINTS, edges)

    print(f"engine root: {root}")
    print(f"modules: {len(modules)}")
    print()
    print(f"  wired in  (another module consumes it): {len(wired)}")
    print(f"  leaf      (only its own .c includes it): {len(leaf)}")
    print(f"  unwired   (nothing in src/ includes it): {len(unwired)}")
    print()
    print(f"reachable from {', '.join(ENTRY_POINTS)}: {len(reach)} of {len(modules)}")
    print()
    print("  REACHABLE  " + ", ".join(sorted(reach)))
    print()
    print("  NOT REACHABLE  " + ", ".join(sorted(set(modules) - reach)))
    print()
    print("LEAF modules (capability, no consumer):")
    for m in leaf:
        print(f"    {m}")
    print()
    print("UNWIRED modules:")
    for m in unwired:
        print(f"    {m}")
    print()
    print("WIRED modules and their consumers:")
    for m, cs in sorted(wired):
        print(f"    {m:24s} <- {', '.join(cs)}")


if __name__ == "__main__":
    main()
