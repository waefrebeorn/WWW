/*
 * wb_quic_path.c -- LP-93 stone 13: path validation and migration.
 */
#include "wb_quic_path.h"

#include <string.h>

void wb_quic_path_init(wb_quic_path *p, uint64_t now_us) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->now_us = now_us;
}

int wb_quic_path_start(wb_quic_path *p, const uint8_t data[WB_QUIC_PATH_DATA_LEN],
                       int expanded) {
    if (!p || !data) return -1;
    for (int i = 0; i < WB_QUIC_PATH_MAX; i++) {
        if (p->probes[i].used) continue;
        memcpy(p->probes[i].data, data, WB_QUIC_PATH_DATA_LEN);
        p->probes[i].sent_us = p->now_us;
        p->probes[i].used = 1;
        p->probes[i].answered = 0;
        p->probes[i].expanded = expanded;
        return 0;
    }
    return -1;
}

int wb_quic_path_response(wb_quic_path *p, const uint8_t data[WB_QUIC_PATH_DATA_LEN]) {
    if (!p || !data) return -1;
    for (int i = 0; i < WB_QUIC_PATH_MAX; i++) {
        wb_quic_probe *pr = &p->probes[i];
        if (!pr->used || pr->answered) continue;
        if (memcmp(pr->data, data, WB_QUIC_PATH_DATA_LEN) != 0) continue;
        pr->answered = 1;
        if (!p->validated) {
            p->validated = 1;
            /* RFC 9000 section 8.2.3: a challenge sent in a datagram shorter
             * than 1200 bytes validates the address but not the path MTU. */
            if (pr->expanded) p->mtu_validated = 1;
            return 1;
        }
        return 0;
    }
    return -1;   /* unsolicited */
}

int wb_quic_path_tick(wb_quic_path *p, uint64_t pto_us) {
    if (!p) return 0;
    /* RFC 9000 section 8.2.4 recommends three times the larger of the current
     * PTO and the new path's PTO; the caller passes that. */
    uint64_t limit = 3u * pto_us;
    int outstanding = 0;
    for (int i = 0; i < WB_QUIC_PATH_MAX; i++) {
        wb_quic_probe *pr = &p->probes[i];
        if (!pr->used || pr->answered) continue;
        if (p->now_us - pr->sent_us >= limit) {
            pr->used = 0;
            continue;
        }
        outstanding++;
    }
    /* losing one challenge is not failure; only the last one is */
    if (outstanding == 0 && !p->validated && !p->abandoned) {
        int any = 0;
        for (int i = 0; i < WB_QUIC_PATH_MAX; i++)
            if (p->probes[i].used) any = 1;
        if (!any) {
            p->abandoned = 1;
            return 1;
        }
    }
    return 0;
}

void wb_quic_path_abandon(wb_quic_path *p) {
    if (!p) return;
    for (int i = 0; i < WB_QUIC_PATH_MAX; i++) p->probes[i].used = 0;
    p->abandoned = 1;
}

int wb_quic_path_validated(const wb_quic_path *p) {
    return p ? p->validated : 0;
}

int wb_quic_path_mtu_validated(const wb_quic_path *p) {
    return p ? p->mtu_validated : 0;
}

int wb_quic_path_abandoned(const wb_quic_path *p) {
    return p ? p->abandoned : 0;
}

int wb_quic_path_probe_count(const wb_quic_path *p) {
    if (!p) return 0;
    int n = 0;
    for (int i = 0; i < WB_QUIC_PATH_MAX; i++)
        if (p->probes[i].used) n++;
    return n;
}
