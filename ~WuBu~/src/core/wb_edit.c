/*
 * wb_edit.c -- WuBu Engine in-world editor (slice 17). See wb_edit.h.
 */
#include "wb_edit.h"
#include "wb_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    wb_edit_op op;
    wb_edit_op inverse;   /* what to apply to undo */
    int        valid;
} wb_edit_entry;

struct wb_editor {
    wb_world   *w;
    wb_edit_entry *ops;        /* the full edit list (the document) */
    int         nops;
    int         cap;

    int        *undo;          /* indices into ops, in order */
    int         nundo;
    int         cap_undo;
    int         cursor;        /* ops [0, cursor) are "current" */

    char        err[256];
};

static void set_err(wb_editor *ed, const char *msg) {
    snprintf(ed->err, sizeof ed->err, "%s", msg);
}

const char *wb_editor_error(const wb_editor *ed) {
    return (ed && ed->err[0]) ? ed->err : NULL;
}

wb_editor *wb_editor_new(wb_world *w) {
    wb_editor *ed = (wb_editor *)calloc(1, sizeof *ed);
    if (!ed) return NULL;
    ed->w = w;
    ed->cap = 256;
    ed->ops = (wb_edit_entry *)calloc((size_t)ed->cap, sizeof *ed->ops);
    ed->cap_undo = 256;
    ed->undo = (int *)calloc((size_t)ed->cap_undo, sizeof(int));
    if (!ed->ops || !ed->undo) { wb_editor_free(ed); return NULL; }
    return ed;
}

void wb_editor_free(wb_editor *ed) {
    if (!ed) return;
    free(ed->ops);
    free(ed->undo);
    free(ed);
}

int wb_editor_count(const wb_editor *ed) { return ed ? ed->cursor : 0; }
int wb_editor_can_undo(const wb_editor *ed) { return ed && ed->cursor > 0; }
int wb_editor_can_redo(const wb_editor *ed) { return ed && ed->cursor < ed->nops; }

/* ------------------------------------------------------------- applying */

static int comp_index(wb_world *w, const char *name) {
    wb_comp_type t = wb_world_find_comp(w, name);
    return t == (wb_comp_type)-1 ? -1 : (int)t;
}

static int apply_op(wb_editor *ed, const wb_edit_op *op, uint32_t *created) {
    if (created) *created = 0;
    wb_world *w = ed->w;
    switch (op->kind) {
        case WB_EDIT_PLACE: {
            int pos_t = comp_index(w, "position");
            int vel_t = comp_index(w, "velocity");
            int mas_t = comp_index(w, "mass");
            int lbl_t = comp_index(w, "label");
            if (pos_t < 0 || vel_t < 0 || mas_t < 0 || lbl_t < 0) {
                set_err(ed, "world is missing position/velocity/mass/label components");
                return -1;
            }
            wb_entity e = wb_entity_new(w);
            float *p = (float *)wb_comp_add(w, e, (wb_comp_type)pos_t);
            float *v = (float *)wb_comp_add(w, e, (wb_comp_type)vel_t);
            float *m = (float *)wb_comp_add(w, e, (wb_comp_type)mas_t);
            char *lbl = (char *)wb_comp_add(w, e, (wb_comp_type)lbl_t);
            if (p) { p[0]=op->a[0]; p[1]=op->a[1]; p[2]=op->a[2]; }
            if (v) { v[0]=v[1]=v[2]=0.0f; }
            if (m) { *m = 1.0f; }
            if (lbl) snprintf(lbl, 32, "%s", op->label);
            if (created) *created = e.index;
            return 0;
        }
        case WB_EDIT_MOVE: {
            int pos_t = comp_index(w, "position");
            if (pos_t < 0) { set_err(ed, "move: world has no position component"); return -1; }
            float *p = (float *)wb_comp_get(w, (wb_entity){ op->entity, 0 }, (wb_comp_type)pos_t);
            if (!p) { set_err(ed, "move: entity has no position component"); return -1; }
            p[0] = op->a[0]; p[1] = op->a[1]; p[2] = op->a[2];
            return 0;
        }
        case WB_EDIT_REMOVE: {
            wb_entity e = { op->entity, 0 };
            if (!wb_entity_valid(w, e)) { set_err(ed, "remove: entity is not valid"); return -1; }
            wb_entity_free(w, e);
            return 0;
        }
        case WB_EDIT_SET_COMP: {
            void *c = wb_comp_get(w, (wb_entity){ op->entity, 0 }, op->comp);
            if (!c) { set_err(ed, "set_comp: component missing"); return -1; }
            /* Copy only as many bytes as the component actually holds -- a
             * 3-float memcpy into a 4-byte mass would overrun the column. */
            size_t bytes = wb_world_comp_elem_size(w, op->comp);
            if (bytes > 3 * sizeof(float)) bytes = 3 * sizeof(float);
            memcpy(c, op->a, bytes);
            return 0;
        }
    }
    set_err(ed, "unknown edit kind");
    return -1;
}

/* Build the inverse of an op, for undo. */
static void make_inverse(wb_editor *ed, const wb_edit_op *op, wb_edit_op *inv) {
    memset(inv, 0, sizeof *inv);
    wb_world *w = ed->w;
    switch (op->kind) {
        case WB_EDIT_PLACE:
            /* The inverse needs the entity index, which apply_op assigned.
             * It is filled in by wb_editor_apply after the fact. */
            inv->kind = WB_EDIT_REMOVE;
            snprintf(inv->label, sizeof inv->label, "%s", op->label);
            break;
        case WB_EDIT_MOVE: {
            inv->kind = WB_EDIT_MOVE;
            inv->entity = op->entity;
            int pos_t = comp_index(w, "position");
            float *p = pos_t >= 0
                ? (float *)wb_comp_get(w, (wb_entity){ op->entity, 0 }, (wb_comp_type)pos_t)
                : NULL;
            if (p) { inv->a[0]=p[0]; inv->a[1]=p[1]; inv->a[2]=p[2]; }
            break;
        }
        case WB_EDIT_REMOVE:
            /* Cannot restore a destroyed entity; record enough to report it. */
            inv->kind = WB_EDIT_REMOVE;
            snprintf(inv->label, sizeof inv->label, "%s", op->label);
            break;
        case WB_EDIT_SET_COMP: {
            inv->kind = WB_EDIT_SET_COMP;
            inv->entity = op->entity;
            inv->comp = op->comp;
            void *c = wb_comp_get(w, (wb_entity){ op->entity, 0 }, op->comp);
            if (c) {
                size_t bytes = wb_world_comp_elem_size(w, op->comp);
                if (bytes > sizeof inv->a) bytes = sizeof inv->a;
                memcpy(inv->a, c, bytes);
            }
            break;
        }
    }
}

int wb_editor_apply(wb_editor *ed, const wb_edit_op *op) {
    if (!ed || !op) return -1;

    if (ed->nops >= WB_EDIT_MAX_OPS) { set_err(ed, "edit list is full"); return -1; }

    /* Capture the pre-state so the inverse is exact. */
    wb_edit_op inverse;
    make_inverse(ed, op, &inverse);

    uint32_t created = 0;
    if (apply_op(ed, op, &created) != 0) return -1;

    /* For PLACE, the inverse removes exactly the entity that was created. */
    if (op->kind == WB_EDIT_PLACE) {
        inverse.entity = created;
    }

    if (ed->nops >= ed->cap) {
        int nc = ed->cap * 2;
        wb_edit_entry *no = (wb_edit_entry *)realloc(ed->ops, (size_t)nc * sizeof *no);
        if (!no) { set_err(ed, "out of memory"); return -1; }
        ed->ops = no;
        ed->cap = nc;
    }
    ed->ops[ed->nops].op = *op;
    ed->ops[ed->nops].inverse = inverse;
    ed->ops[ed->nops].valid = 1;
    ed->nops++;
    ed->cursor = ed->nops;
    return 0;
}

int wb_editor_live(wb_editor *ed, const wb_edit_op *op) {
    return wb_editor_apply(ed, op);
}

int wb_editor_undo(wb_editor *ed) {
    if (!ed || ed->cursor <= 0) return -1;
    ed->cursor--;
    wb_edit_entry *e = &ed->ops[ed->cursor];
    if (!e->valid) return -1;
    apply_op(ed, &e->inverse, NULL);
    return 0;
}

int wb_editor_redo(wb_editor *ed) {
    if (!ed || ed->cursor >= ed->nops) return -1;
    wb_edit_entry *e = &ed->ops[ed->cursor];
    if (!e->valid) return -1;
    apply_op(ed, &e->op, NULL);
    ed->cursor++;
    return 0;
}

/* -------------------------------------------------------- serialization */

static const char *kind_name(int k) {
    switch (k) {
        case WB_EDIT_PLACE: return "place";
        case WB_EDIT_MOVE: return "move";
        case WB_EDIT_REMOVE: return "remove";
        case WB_EDIT_SET_COMP: return "set_comp";
    }
    return "?";
}

int wb_editor_save(const wb_editor *ed, const char *path) {
    if (!ed || !path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) { return -1; }
    fprintf(f, "{\n  \"ops\": [\n");
    for (int i = 0; i < ed->cursor; i++) {
        const wb_edit_op *op = &ed->ops[i].op;
        fprintf(f, "    {\"kind\": \"%s\", \"entity\": %u, \"comp\": %u, "
                   "\"a\": [%.5f, %.5f, %.5f], \"label\": \"%s\"}%s\n",
                kind_name(op->kind), op->entity, op->comp,
                op->a[0], op->a[1], op->a[2], op->label,
                (i + 1 < ed->cursor) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return 0;
}

int wb_editor_replay(wb_editor *ed, const char *path) {
    if (!ed || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);

    wb_json *root = wb_json_parse(buf, got);
    free(buf);
    if (!root) { set_err(ed, "replay JSON parse failed"); return -1; }
    wb_json *ops = wb_json_get(root, "ops");
    if (!ops) { wb_json_free(root); set_err(ed, "replay has no ops array"); return -1; }

    int applied = 0;
    for (int i = 0; i < ops->n; i++) {
        wb_json *o = ops->items[i];
        wb_edit_op op;
        memset(&op, 0, sizeof op);
        const char *k = wb_json_str(wb_json_get(o, "kind"));
        if (!k) continue;
        if (!strcmp(k, "place"))        op.kind = WB_EDIT_PLACE;
        else if (!strcmp(k, "move"))    op.kind = WB_EDIT_MOVE;
        else if (!strcmp(k, "remove"))  op.kind = WB_EDIT_REMOVE;
        else if (!strcmp(k, "set_comp")) op.kind = WB_EDIT_SET_COMP;
        else continue;
        op.entity = (uint32_t)wb_json_int(wb_json_get(o, "entity"), 0);
        op.comp   = (uint32_t)wb_json_int(wb_json_get(o, "comp"), 0);
        wb_json *a = wb_json_get(o, "a");
        if (a && a->type == WB_JSON_ARR) {
            for (int c = 0; c < 3 && c < a->n; c++) op.a[c] = (float)a->items[c]->num;
        }
        const char *lbl = wb_json_str(wb_json_get(o, "label"));
        if (lbl) snprintf(op.label, sizeof op.label, "%s", lbl);
        if (wb_editor_apply(ed, &op) != 0) break;
        applied++;
    }
    wb_json_free(root);
    return applied > 0 ? 0 : -1;
}
