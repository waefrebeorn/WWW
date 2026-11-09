/*
 * wb_edit.h -- WuBu Engine in-world editor (slice 17).
 *
 * A document model for world edits: every change is an operation, applied to
 * the live world immediately (live edit) and recorded so it can be undone,
 * redone, saved and replayed. This is the substrate for an in-engine editor:
 * the same ops a creator makes by hand are the ones a tool or script can emit.
 *
 * C11, no external deps.
 */
#ifndef WB_EDIT_H
#define WB_EDIT_H

#include "wb_ecs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WB_EDIT_MAX_OPS   4096
#define WB_EDIT_MAX_UNDO  4096
#define WB_EDIT_LABEL_LEN 64

typedef enum {
    WB_EDIT_PLACE = 0,    /* create an entity at a position */
    WB_EDIT_MOVE,         /* change an entity's position */
    WB_EDIT_REMOVE,       /* destroy an entity */
    WB_EDIT_SET_COMP      /* write a component value */
} wb_edit_kind;

typedef struct {
    int      kind;                       /* wb_edit_kind */
    uint32_t entity;                     /* target entity index */
    uint32_t comp;                       /* component type for SET_COMP */
    float    a[3];                       /* position / new value */
    float    b[3];                       /* secondary value */
    char     label[WB_EDIT_LABEL_LEN];   /* entity label for PLACE/REMOVE */
} wb_edit_op;

typedef struct wb_editor wb_editor;

wb_editor *wb_editor_new(wb_world *w);
void       wb_editor_free(wb_editor *ed);

/* Apply an op to the world and record it on the undo stack. Returns 0 on
 * success. */
int  wb_editor_apply(wb_editor *ed, const wb_edit_op *op);

/* Live edit: identical to apply -- named separately so tooling can express
 * intent (an edit made by a human this frame, not a replay). */
int  wb_editor_live(wb_editor *ed, const wb_edit_op *op);

int  wb_editor_undo(wb_editor *ed);
int  wb_editor_redo(wb_editor *ed);
int  wb_editor_can_undo(const wb_editor *ed);
int  wb_editor_can_redo(const wb_editor *ed);
int  wb_editor_count(const wb_editor *ed);

/* Save/replay the edit list as JSON. Returns 0 on success. */
int  wb_editor_save(const wb_editor *ed, const char *path);
int  wb_editor_replay(wb_editor *ed, const char *path);

const char *wb_editor_error(const wb_editor *ed);

#ifdef __cplusplus
}
#endif

#endif /* WB_EDIT_H */
