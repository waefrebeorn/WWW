/*
 * wb_watch_test.c -- LP-38 stone 1 gate: file change detection.
 *
 * Devil's-advocate cases: a rewrite with identical bytes is not a change, an
 * appearing or disappearing file is, an unknown path is reported, and a full
 * watch list refuses to grow.
 */
#include "wb_watch.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define PATH "build/wb_watch_test.tmp"

static void write_file(const char *text) {
    FILE *f = fopen(PATH, "wb");
    if (!f) return;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

static void append_file(const char *text) {
    FILE *f = fopen(PATH, "ab");
    if (!f) return;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

static void delete_file(void) { remove(PATH); }

int main(void) {
    delete_file();   /* a clean start */

    /* ---- a file that does not exist yet ---- */
    wb_watch w;
    CHECK(wb_watch_init(&w, 4) == 0, "the watcher is created");
    CHECK(wb_watch_add(&w, PATH) == 0, "a missing file can be watched");
    CHECK(wb_watch_poll(&w, PATH) == 0, "a still-missing file is unchanged");

    /* ---- the file appears ---- */
    write_file("hello");
    CHECK(wb_watch_poll(&w, PATH) == 1, "an appearing file is a change");
    CHECK(wb_watch_refresh(&w, PATH) == 0, "refresh resnapshots the file");
    CHECK(wb_watch_poll(&w, PATH) == 0, "the refreshed file is unchanged");

    /* ---- an append is a change ---- */
    append_file(" world");
    CHECK(wb_watch_poll(&w, PATH) == 1, "an append is a change");
    wb_watch_refresh(&w, PATH);

    /* ---- a rewrite with identical bytes is not a change ---- */
    write_file("hello world");
    CHECK(wb_watch_poll(&w, PATH) == 0, "rewriting identical bytes is not a change");

    /* ---- a truncation is a change ---- */
    write_file("hi");
    CHECK(wb_watch_poll(&w, PATH) == 1, "a truncation is a change");
    wb_watch_refresh(&w, PATH);

    /* ---- the file disappears ---- */
    delete_file();
    CHECK(wb_watch_poll(&w, PATH) == 1, "a disappearing file is a change");
    CHECK(wb_watch_poll(&w, PATH) == 0, "a still-missing file is unchanged");

    /* ---- it reappears with the same bytes it had before it vanished ---- */
    write_file("hi");
    CHECK(wb_watch_poll(&w, PATH) == 1, "reappearing is a change even with the same bytes");

    /* ---- an unknown path ---- */
    CHECK(wb_watch_poll(&w, "build/no_such_file.tmp") == -1, "an unwatched path is reported");
    CHECK(wb_watch_refresh(&w, "build/no_such_file.tmp") == -1, "refreshing an unwatched path fails");

    /* ---- adding the same path twice is idempotent ---- */
    CHECK(wb_watch_add(&w, PATH) == 0, "re-adding a watched path succeeds");
    CHECK(wb_watch_poll(&w, PATH) == 0, "re-adding does not reset the snapshot");

    /* ---- a full watch list refuses to grow ---- */
    wb_watch small;
    wb_watch_init(&small, 2);
    CHECK(wb_watch_add(&small, "a.tmp") == 0, "the first slot is taken");
    CHECK(wb_watch_add(&small, "b.tmp") == 0, "the second slot is taken");
    CHECK(wb_watch_add(&small, "c.tmp") == -1, "a full watch list refuses to grow");

    /* ---- clear ---- */
    wb_watch_clear(&w);
    CHECK(wb_watch_poll(&w, PATH) == -1, "clear drops every watch");

    /* ---- determinism ---- */
    delete_file();
    write_file("determinism");
    wb_watch a, b;
    wb_watch_init(&a, 4);
    wb_watch_init(&b, 4);
    wb_watch_add(&a, PATH);
    wb_watch_add(&b, PATH);
    CHECK(wb_watch_poll(&a, PATH) == wb_watch_poll(&b, PATH), "polling is deterministic");

    /* ---- degenerate input ---- */
    CHECK(wb_watch_init(NULL, 4) == -1, "a NULL watcher is rejected");
    CHECK(wb_watch_init(&w, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_watch_add(NULL, PATH) == -1, "a NULL watcher rejects an add");
    CHECK(wb_watch_add(&w, NULL) == -1, "a NULL path is rejected");

    wb_watch_free(&w);
    wb_watch_free(&small);
    wb_watch_free(&a);
    wb_watch_free(&b);
    delete_file();

    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
