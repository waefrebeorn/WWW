/*
 * wb_meshlet.c -- meshlet partitioning (LP-10 stone 2). See wb_meshlet.h.
 */
#include "wb_meshlet.h"

#include <stdlib.h>
#include <string.h>

struct wb_meshlet_builder {
    const uint32_t *indices;
    uint32_t ntri;
    int max_verts, max_tris;

    /* the meshlet being accumulated */
    uint32_t *cur_verts;       /* unique global vertex indices */
    int cur_nverts, cur_cap;
    uint32_t *cur_tris;        /* 3 global indices per triangle */
    uint32_t cur_ntris;
};

static int wb_meshlet_push_vert(struct wb_meshlet_builder *b, uint32_t v) {
    for (int i = 0; i < b->cur_nverts; i++) if (b->cur_verts[i] == v) return 0;
    if (b->cur_nverts == b->cur_cap) {
        int nc = b->cur_cap ? b->cur_cap * 2 : 16;
        uint32_t *nv = (uint32_t *)realloc(b->cur_verts, (size_t)nc * sizeof(uint32_t));
        if (!nv) return -1;
        b->cur_verts = nv;
        b->cur_cap = nc;
    }
    b->cur_verts[b->cur_nverts++] = v;
    return 1;
}

int wb_meshlets_build(const uint32_t *indices, uint32_t nindices,
                      int max_verts, int max_tris, wb_meshlets *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!indices || nindices == 0 || nindices % 3 != 0) return -1;
    if (max_verts < 3) max_verts = 64;
    if (max_tris < 1) max_tris = 128;

    struct wb_meshlet_builder b;
    memset(&b, 0, sizeof b);
    b.indices = indices;
    b.ntri = nindices / 3;
    b.max_verts = max_verts;
    b.max_tris = max_tris;

    wb_meshlet_header *headers = NULL;
    uint32_t *verts = NULL, *lidx = NULL;
    uint32_t n_headers = 0, n_verts = 0, n_lidx = 0;
    int rc = -1;

    for (uint32_t t = 0; t < b.ntri; t++) {
        const uint32_t *tri = indices + t * 3;

        /* how many NEW vertices would this triangle add? */
        int new_verts = 0;
        for (int j = 0; j < 3; j++) {
            int found = 0;
            for (int i = 0; i < b.cur_nverts; i++) if (b.cur_verts[i] == tri[j]) { found = 1; break; }
            if (!found) new_verts++;
        }
        int would_verts = b.cur_nverts + new_verts;

        /* flush when the triangle cannot join the current meshlet */
        if (b.cur_ntris > 0 && (would_verts > b.max_verts || b.cur_ntris >= (uint32_t)b.max_tris)) {
            /* append the accumulated meshlet */
            uint32_t need_v = n_verts + (uint32_t)b.cur_nverts;
            uint32_t need_i = n_lidx + b.cur_ntris * 3;
            wb_meshlet_header *nh = (wb_meshlet_header *)realloc(headers, (n_headers + 1) * sizeof(*nh));
            if (!nh) goto done;
            headers = nh;
            uint32_t *nv = (uint32_t *)realloc(verts, need_v * sizeof(uint32_t));
            if (!nv) goto done;
            verts = nv;
            uint32_t *ni = (uint32_t *)realloc(lidx, need_i * sizeof(uint32_t));
            if (!ni) goto done;
            lidx = ni;

            wb_meshlet_header *h = &headers[n_headers++];
            h->vert_offset = n_verts;
            h->index_offset = n_lidx;
            h->nverts = (uint32_t)b.cur_nverts;
            h->ntris = b.cur_ntris;
            memcpy(verts + n_verts, b.cur_verts, (size_t)b.cur_nverts * sizeof(uint32_t));
            /* local indices: map each global vertex to its slot in this meshlet */
            for (uint32_t k = 0; k < b.cur_ntris; k++) {
                for (int j = 0; j < 3; j++) {
                    uint32_t g = b.cur_tris[k * 3 + j];
                    uint32_t slot = 0;
                    for (int i = 0; i < b.cur_nverts; i++) if (b.cur_verts[i] == g) { slot = (uint32_t)i; break; }
                    lidx[n_lidx + k * 3 + j] = slot;
                }
            }
            n_verts = need_v;
            n_lidx = need_i;
            b.cur_nverts = 0;
            b.cur_ntris = 0;
        }

        /* add the triangle's vertices, then the triangle */
        for (int j = 0; j < 3; j++)
            if (wb_meshlet_push_vert(&b, tri[j]) != 0) { /* pushed, or already present */ }
        if (b.cur_ntris >= (uint32_t)b.max_tris) goto done;   /* cannot happen */
        b.cur_tris = (uint32_t *)realloc(b.cur_tris, (b.cur_ntris + 1) * 3 * sizeof(uint32_t));
        if (!b.cur_tris) goto done;
        b.cur_tris[b.cur_ntris * 3 + 0] = tri[0];
        b.cur_tris[b.cur_ntris * 3 + 1] = tri[1];
        b.cur_tris[b.cur_ntris * 3 + 2] = tri[2];
        b.cur_ntris++;
    }

    /* flush the last meshlet */
    if (b.cur_ntris > 0) {
        uint32_t need_v = n_verts + (uint32_t)b.cur_nverts;
        uint32_t need_i = n_lidx + b.cur_ntris * 3;
        wb_meshlet_header *nh = (wb_meshlet_header *)realloc(headers, (n_headers + 1) * sizeof(*nh));
        if (!nh) goto done;
        headers = nh;
        uint32_t *nv = (uint32_t *)realloc(verts, need_v * sizeof(uint32_t));
        if (!nv) goto done;
        verts = nv;
        uint32_t *ni = (uint32_t *)realloc(lidx, need_i * sizeof(uint32_t));
        if (!ni) goto done;
        lidx = ni;

        wb_meshlet_header *h = &headers[n_headers++];
        h->vert_offset = n_verts;
        h->index_offset = n_lidx;
        h->nverts = (uint32_t)b.cur_nverts;
        h->ntris = b.cur_ntris;
        memcpy(verts + n_verts, b.cur_verts, (size_t)b.cur_nverts * sizeof(uint32_t));
        for (uint32_t k = 0; k < b.cur_ntris; k++) {
            for (int j = 0; j < 3; j++) {
                uint32_t g = b.cur_tris[k * 3 + j];
                uint32_t slot = 0;
                for (int i = 0; i < b.cur_nverts; i++) if (b.cur_verts[i] == g) { slot = (uint32_t)i; break; }
                lidx[n_lidx + k * 3 + j] = slot;
            }
        }
        n_verts = need_v;
        n_lidx = need_i;
    }

    out->headers = headers;
    out->verts = verts;
    out->indices = lidx;
    out->n_meshlets = n_headers;
    rc = 0;

done:
    free(b.cur_verts);
    free(b.cur_tris);
    if (rc != 0) {
        free(headers); free(verts); free(lidx);
        memset(out, 0, sizeof(*out));
    }
    return rc;
}

void wb_meshlets_free(wb_meshlets *m) {
    if (!m) return;
    free(m->headers);
    free(m->verts);
    free(m->indices);
    memset(m, 0, sizeof(*m));
}
