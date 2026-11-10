/*
 * wb_prof.h -- frame profiler (LP-01 stone 1).
 *
 * Zero-dependency instrumentation primitives:
 *   - named counters with a pointer-to-accumulator hot path:
 *       double *a = wb_prof_counter(p, "draws");   // one lookup per stream
 *       *a += n;                                    // no work per call
 *   - manual zone scopes: wb_prof_zone_begin/end name bounds in the frame.
 *   - a per-slot ring of the last N frame values; wb_prof_stat() folds it.
 *
 * Counters and zones share one slot store. Counters are running totals whose
 * ring holds each frame's delta; zones are per-frame seconds (accum resets).
 * Every live slot records every frame, so rings are garbage-free.
 *
 * No threads, no allocations outside wb_prof_new, no engine deps.
 */
#ifndef WB_PROF_H
#define WB_PROF_H

#include <stdio.h>

enum {
    WB_PROF_STRLEN   = 40,
    WB_PROF_MAXSLOTS = 128,
    WB_PROF_NBUCKET  = 256
};

typedef struct wb_prof_slot {
    char   name[WB_PROF_STRLEN];
    int    is_zone;
    int    zones;        /* open/close events into the zone this frame */
    double accum;        /* counter running total, or zone seconds this frame */
    double frame;        /* counter total at last end_frame (for the delta) */
    int    written;      /* frames recorded into the ring (chronological) */
    float  ring[1];      /* per-frame values; length = wb_prof ring */
} wb_prof_slot;

typedef struct {
    int    ring;         /* how many frames the ring keeps */
    int    nslots;
    int    frames;       /* end_frames seen */
    wb_prof_slot *slot[WB_PROF_MAXSLOTS];  /* by first-fit index */
    int    bucket[WB_PROF_NBUCKET];        /* open-addressed, stores idx+1 */
} wb_prof;

typedef struct {
    int   n;             /* frames recorded in the window (<= ring) */
    float last, mean, mmin, mmax;
} wb_prof_stat;

wb_prof *wb_prof_new(int ring);
void     wb_prof_dispose(wb_prof *p);

/* engine-wide default profiler, lazily created (ring 256). */
wb_prof *wb_prof_default(void);

void     wb_prof_begin_frame(wb_prof *p);
void     wb_prof_end_frame(wb_prof *p);

/* accumulator for `name`; valid for the lifetime of `p`. */
double  *wb_prof_counter(wb_prof *p, const char *name);
/* open a timed zone; must be closed (LIFO) before end_frame. */
void     wb_prof_zone_begin(wb_prof *p, const char *name);
void     wb_prof_zone_end(wb_prof *p, const char *name);

int      wb_prof_summary(wb_prof *p, const char *name, wb_prof_stat *out);
int      wb_prof_report(wb_prof *p, FILE *f);

/* convenience scoping macro: WB_PROF_ZONE(p, "name") { ... } */
#define WB_PROF_ZONE(_p, _name) \
    for (int _zz = (wb_prof_zone_begin((_p), (_name)), 1); _zz; \
         wb_prof_zone_end((_p), (_name)), _zz = 0)

#endif /* WB_PROF_H */
