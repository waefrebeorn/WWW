/*
 * wb_patch.h -- line patch applier (LP-48 stone 1).
 *
 * A patch is a list of hunks: at line L, remove N lines and insert M. Applying a
 * patch to a text is the primitive under merge, under three-way conflict
 * resolution, and under any "edit this file for me" tool. Hunks are applied from
 * the bottom up so that an earlier hunk's line numbers are not disturbed by a
 * later one, and a hunk that does not fit the text is refused rather than
 * misapplied.
 *
 * C11, no deps.
 */
#ifndef WB_PATCH_H
#define WB_PATCH_H

#include <stdint.h>

typedef struct {
    uint32_t       line;      /* the 0-based line to start at */
    uint32_t       nremove;   /* how many lines to remove there */
    uint32_t       nadd;      /* how many lines to insert */
    const char   **add;       /* the inserted lines (borrowed) */
} wb_patch_hunk;

typedef struct {
    char    **lines;          /* owned */
    uint32_t  n;
} wb_patch_result;

/* Split text into lines. `text` is borrowed; the result owns copies. A trailing
 * newline does not create a final empty line. */
int  wb_patch_split(const char *text, wb_patch_result *out);
void wb_patch_result_free(wb_patch_result *r);

/* Apply hunks to `text`, bottom-up. Returns 0 on success, -1 if any hunk does not
 * fit (nothing is written to `out` in that case). */
int  wb_patch_apply(const char *text, const wb_patch_hunk *hunks, uint32_t nhunks, wb_patch_result *out);

/* Join lines back into a single string (owned). Returns NULL on allocation
 * failure. */
char *wb_patch_join(const wb_patch_result *r);

#endif /* WB_PATCH_H */
