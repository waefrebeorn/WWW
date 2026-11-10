#!/bin/sh
# Sync the hive memory modules from WuBuHIVE into the engine.
#
# WuBuHIVE (github.com/waefrebeorn/WuBuHIVE) is UPSTREAM for these files: it is
# where the hive substrate is extracted for reuse across all wubu projects.
# Fix them there, run this, then commit both repos.
#
#   sh scripts/sync-wubuhive.sh [path-to-WuBuHIVE]
#
# The copies carry the same `wb_` prefix, so this is a byte-for-byte copy and
# any divergence shows up as a plain diff.
set -e

HERE=$(cd "$(dirname "$0")/.." && pwd)
HIVE="${1:-$(cd "$HERE/../../.." && pwd)/WuBuHIVE}"

if [ ! -d "$HIVE/src" ]; then
    echo "error: $HIVE does not look like WuBuHIVE (no src/)" >&2
    exit 1
fi

HEADERS="wb_hive.h wb_tagheap.h wb_budget.h wb_frame.h wb_atomic.h wb_pool.h wb_tls.h"
SOURCES="wb_hive.c wb_tagheap.c wb_budget.c wb_frame.c wb_pool.c wb_tls.c"
TESTS="wb_hive_test.c wb_tagheap_test.c wb_budget_test.c wb_frame_test.c wb_atomic_test.c wb_pool_test.c wb_threadheap_test.c wb_tls_test.c"

for f in $HEADERS $SOURCES; do
    cp "$HIVE/include/$f" "$HERE/src/core/$f" 2>/dev/null \
        || cp "$HIVE/src/$f" "$HERE/src/core/$f"
done
for f in $TESTS; do
    cp "$HIVE/tests/$f" "$HERE/tests/$f"
done

echo "synced from $HIVE"
echo "  headers : $HEADERS"
echo "  sources : $SOURCES"
echo "  tests   : $TESTS"
echo
echo "The Makefile already wires wb_tls.c into CORE_SRC, test-tagheap,"
echo "test-threadheap and the test-tls gate -- if you add a new module, add"
echo "it there too."
