/*
 * wb_match.c -- matchmaking team balance (LP-34 stone 1). See wb_match.h.
 */
#include "wb_match.h"

#include <stdlib.h>

static int wb_match_cmp(const void *a, const void *b, void *ctx) {
    const uint32_t *skill = (const uint32_t *)ctx;
    uint32_t ia = *(const uint32_t *)a, ib = *(const uint32_t *)b;
    if (skill[ia] != skill[ib]) return skill[ia] > skill[ib] ? -1 : 1;   /* descending */
    return ia < ib ? -1 : (ia > ib ? 1 : 0);                             /* stable by index */
}

/* qsort_r is not portable; a simple insertion sort keeps the tie-break stable
 * and n is small (a match is tens of players). */
static void wb_match_sort_desc(const uint32_t *skill, uint32_t *idx, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = idx[i];
        uint32_t j = i;
        while (j > 0 && wb_match_cmp(&idx[j - 1], &key, (void *)skill) > 0) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = key;
    }
}

uint32_t wb_match_balance(const uint32_t *skill, uint32_t n, uint8_t *out_team) {
    if (!skill || !out_team || n == 0) return 0;
    uint32_t *idx = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!idx) return 0;
    for (uint32_t i = 0; i < n; i++) idx[i] = i;
    wb_match_sort_desc(skill, idx, n);

    uint32_t total[2] = { 0, 0 };
    for (uint32_t k = 0; k < n; k++) {
        uint32_t p = idx[k];
        uint32_t t = (total[1] < total[0]) ? 1 : 0;   /* the weaker team; ties to 0 */
        out_team[p] = (uint8_t)t;
        total[t] += skill[p];
    }
    free(idx);
    return total[0] > total[1] ? total[0] - total[1] : total[1] - total[0];
}

uint32_t wb_match_team_skill(const uint32_t *skill, const uint8_t *team, uint32_t n, uint32_t which) {
    if (!skill || !team) return 0;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) if (team[i] == which) sum += skill[i];
    return sum;
}
