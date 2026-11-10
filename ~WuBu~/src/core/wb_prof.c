/*
 * wb_prof.c -- frame profiler (LP-01 stone 1). See wb_prof.h.
 *
 * Design notes for the hot path: a counter is acquired once (wb_prof_counter
 * returns a pointer) and added to every frame with no further work. So the
 * profiler does NOT mark "touched this frame" slots -- every live slot is
 * recorded every frame (an untouched counter naturally records a 0 delta).
 * Rings are therefore garbage-free from the moment a slot is created.
 */
#include "wb_prof.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/* monotonic seconds, enough for sub-microsecond zone timing */
static double wb_prof_now(void) {
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static unsigned wb_prof_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h = (h ^ (unsigned char)*s++) * 16777619u; }
    return h;
}

wb_prof *wb_prof_new(int ring) {
    if (ring < 1) ring = 1;
    if (ring > 4096) ring = 4096;
    wb_prof *p = (wb_prof *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->ring = ring;
    return p;
}

void wb_prof_dispose(wb_prof *p) {
    if (!p) return;
    for (int i = 0; i < p->nslots; i++)
        if (p->slot[i]) free(p->slot[i]);
    free(p);
}

/* open-addressed name lookup. Returns slot, or NULL (writes empty bucket). */
static wb_prof_slot *wb_prof_find(wb_prof *p, const char *name, int *empty_idx) {
    unsigned b = wb_prof_hash(name) & (WB_PROF_NBUCKET - 1);
    for (int probes = 0; probes < WB_PROF_NBUCKET; probes++) {
        int idx = p->bucket[b] - 1;
        if (idx < 0) { if (empty_idx) *empty_idx = (int)b; return NULL; }
        if (strcmp(p->slot[idx]->name, name) == 0) {
            if (empty_idx) *empty_idx = idx;
            return p->slot[idx];
        }
        b = (b + 1) & (WB_PROF_NBUCKET - 1);
    }
    if (empty_idx) *empty_idx = -1;
    return NULL;
}

static wb_prof_slot *wb_prof_acquire(wb_prof *p, const char *name, int is_zone) {
    int at = -1;
    wb_prof_slot *s = wb_prof_find(p, name, &at);
    if (s) return s;
    if (at < 0 || p->nslots >= WB_PROF_MAXSLOTS) return NULL;
    size_t n = sizeof(wb_prof_slot) + (size_t)(p->ring - 1) * sizeof(float);
    s = (wb_prof_slot *)calloc(1, n);
    if (!s) return NULL;
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->is_zone = is_zone;
    p->slot[p->nslots] = s;
    p->bucket[at] = p->nslots + 1;
    p->nslots++;
    return s;
}

void wb_prof_begin_frame(wb_prof *p) { (void)p; /* bookkeeping is end-driven */ }

void wb_prof_end_frame(wb_prof *p) {
    if (!p) return;
    for (int i = 0; i < p->nslots; i++) {
        wb_prof_slot *s = p->slot[i];
        if (!s) continue;
        double v;
        if (s->is_zone) {
            v = s->accum;              /* per-frame seconds, then reset */
            s->accum = 0.0;
            s->zones = 0;
        } else {
            v = s->accum - s->frame;   /* this frame's delta */
            s->frame = s->accum;
        }
        s->ring[s->written % p->ring] = (float)v;
        s->written++;
    }
    p->frames++;
}

wb_prof *wb_prof_default(void) {
    static wb_prof *g = NULL;
    if (!g) g = wb_prof_new(256);
    return g;
}

double *wb_prof_counter(wb_prof *p, const char *name) {
    if (!p || !name) return NULL;
    wb_prof_slot *s = wb_prof_acquire(p, name, 0);
    return s ? &s->accum : NULL;
}

/* --- zones (single global stack; the engine has one profiler) -------- */

typedef struct { wb_prof_slot *slot; double t0; } wb_prof_open;
static wb_prof_open g_open[WB_PROF_MAXSLOTS];
static int g_nopen;
static wb_prof *g_owner;

void wb_prof_zone_begin(wb_prof *p, const char *name) {
    if (!p || !name) return;
    wb_prof_slot *s = wb_prof_acquire(p, name, 1);
    if (!s) return;
    if (g_owner != p) { g_nopen = 0; g_owner = p; }
    if (g_nopen < WB_PROF_MAXSLOTS) {
        g_open[g_nopen].slot = s;
        g_open[g_nopen].t0 = wb_prof_now();
        g_nopen++;
    }
}

void wb_prof_zone_end(wb_prof *p, const char *name) {
    if (!p || !name || g_owner != p) return;
    for (int i = g_nopen - 1; i >= 0; i--) {
        if (strcmp(g_open[i].slot->name, name) == 0) {
            double elapsed = wb_prof_now() - g_open[i].t0;
            g_open[i].slot->accum += elapsed;
            g_open[i].slot->zones++;
            g_open[i] = g_open[g_nopen - 1];
            g_nopen--;
            return;
        }
    }
}

/* --- query ----------------------------------------------------------- */

int wb_prof_summary(wb_prof *p, const char *name, wb_prof_stat *out) {
    if (!p || !out) return 0;
    wb_prof_slot *s = wb_prof_find(p, name, NULL);
    if (!s) return 0;
    int n = s->written < p->ring ? s->written : p->ring;
    out->n = n;
    if (n == 0) { out->last = out->mean = out->mmin = out->mmax = 0.f; return 1; }
    float sum = 0.f;
    float mn = s->ring[(s->written - n) % p->ring];
    float mx = mn;
    for (int k = 0; k < n; k++) {
        float v = s->ring[(s->written - n + k) % p->ring];  /* circular window */
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    out->last = s->ring[(s->written - 1 + p->ring) % p->ring];
    out->mean = sum / (float)n;
    out->mmin = mn;
    out->mmax = mx;
    return 1;
}

int wb_prof_report(wb_prof *p, FILE *f) {
    if (!p || !f) return 0;
    int shown = 0;
    for (int i = 0; i < p->nslots; i++) {
        wb_prof_slot *s = p->slot[i];
        if (!s) continue;
        wb_prof_stat st;
        wb_prof_summary(p, s->name, &st);
        if (s->is_zone)
            fprintf(f, "  %-20s zone  last=%7.2fms mean=%7.2fms min=%7.2f max=%7.2f frames=%d\n",
                    s->name, st.last * 1000.0, st.mean * 1000.0,
                    st.mmin * 1000.0, st.mmax * 1000.0, st.n);
        else
            fprintf(f, "  %-20s cnt   last=%11.1f mean=%11.1f min=%11.1f max=%11.1f frames=%d\n",
                    s->name, st.last, st.mean, st.mmin, st.mmax, st.n);
        shown++;
    }
    return shown;
}
