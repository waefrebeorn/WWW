/*
 * wb_watch.h -- file change detection (LP-38 stone 1).
 *
 * Hot reload is a change detector plus a rebuild: watch a file, notice when its
 * content changed, rebuild the thing that came from it. This is the detector --
 * it keys on a content hash (not a timestamp), so an editor that rewrites a
 * file with the same bytes does not trigger a rebuild, and a clock skew does not
 * either.
 *
 * C11, no deps.
 */
#ifndef WB_WATCH_H
#define WB_WATCH_H

#include <stdint.h>

typedef struct {
    char     path[260];
    uint64_t hash;
    uint64_t size;
    int      exists;
} wb_watch_entry;

typedef struct {
    wb_watch_entry *entries;
    uint32_t        n, cap;
} wb_watch;

int  wb_watch_init(wb_watch *w, uint32_t cap);
void wb_watch_free(wb_watch *w);
void wb_watch_clear(wb_watch *w);

/* Start watching a path, snapshotting its current state. Returns 0 on success,
 * -1 if the watch list is full. */
int  wb_watch_add(wb_watch *w, const char *path);

/* 1 if the file changed since the last poll (including appearing or
 * disappearing), 0 if it is unchanged, -1 if the path is not watched. A change is
 * reported once: the snapshot advances with the report. */
int  wb_watch_poll(wb_watch *w, const char *path);

/* Re-snapshot a watched path. Returns 0 on success, -1 if not watched. */
int  wb_watch_refresh(wb_watch *w, const char *path);

#endif /* WB_WATCH_H */
