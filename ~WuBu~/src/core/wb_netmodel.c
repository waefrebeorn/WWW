/*
 * wb_netmodel.c -- deterministic network model (LP-32 stone 1). See wb_netmodel.h.
 */
#include "wb_netmodel.h"

#include <stdlib.h>

static uint32_t wb_netmodel_rand(wb_netmodel *m) {
    m->rng = m->rng * 1664525u + 1013904223u;   /* Numerical Recipes LCG */
    return m->rng;
}

static float wb_netmodel_rand_unit(wb_netmodel *m) {
    return (float)(wb_netmodel_rand(m) >> 8) * (1.0f / 16777216.0f);   /* [0,1) */
}

void wb_netmodel_init(wb_netmodel *m, const wb_netmodel_cfg *cfg) {
    if (!m || !cfg) return;
    m->cfg = *cfg;
    m->pkts = NULL;
    m->n = 0;
    m->cap = 0;
    m->rng = cfg->seed ? cfg->seed : 0x9E3779B9u;
}

void wb_netmodel_free(wb_netmodel *m) {
    if (!m) return;
    free(m->pkts);
    m->pkts = NULL;
    m->n = m->cap = 0;
}

void wb_netmodel_clear(wb_netmodel *m) {
    if (!m) return;
    m->n = 0;
}

int wb_netmodel_send(wb_netmodel *m, uint32_t seq, double now) {
    if (!m) return 0;
    if (wb_netmodel_rand_unit(m) < m->cfg.loss) return 0;    /* dropped */

    float jitter = (wb_netmodel_rand_unit(m) * 2.0f - 1.0f) * m->cfg.jitter;
    double delay = (double)m->cfg.latency + (double)jitter;

    if (m->n == m->cap) {
        uint32_t nc = m->cap ? m->cap * 2 : 8;
        wb_netmodel_packet *np = (wb_netmodel_packet *)realloc(m->pkts, nc * sizeof(*np));
        if (!np) return 0;
        m->pkts = np;
        m->cap = nc;
    }
    m->pkts[m->n].seq = seq;
    m->pkts[m->n].deliver_at = now + delay;
    m->n++;
    return 1;
}

int wb_netmodel_poll(wb_netmodel *m, double now, uint32_t *seq) {
    if (!m || !m->pkts || m->n == 0) return 0;
    uint32_t best = 0;
    int found = 0;
    for (uint32_t i = 0; i < m->n; i++) {
        if (m->pkts[i].deliver_at > now) continue;
        if (!found || m->pkts[i].deliver_at < m->pkts[best].deliver_at) { best = i; found = 1; }
    }
    if (!found) return 0;
    if (seq) *seq = m->pkts[best].seq;
    m->pkts[best] = m->pkts[m->n - 1];   /* swap-remove */
    m->n--;
    return 1;
}

uint32_t wb_netmodel_pending(const wb_netmodel *m) { return m ? m->n : 0; }
