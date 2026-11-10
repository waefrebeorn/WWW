/*
 * wb_eventlog_test.c -- LP-50 stone 1 gate: the snapshot + tail event log.
 *
 * Devil's-advocate cases: an out-of-order tick is refused rather than silently
 * written, a flipped payload byte is caught before the state is trusted, a
 * truncated file fails verification, rotation really drops the tail, and replay
 * stops at the requested tick instead of reading to the end.
 */
#include "wb_eventlog.h"
#include "wb_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define TEST_TRUNCATE(f, n) _chsize_s(_fileno(f), (__int64)(n))
#else
#include <unistd.h>
#define TEST_TRUNCATE(f, n) ftruncate(fileno(f), (off_t)(n))
#endif

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

#define PATH "build/wb_eventlog_test.tmp"

typedef struct {
    int count;
    uint32_t last_tick;
    uint32_t types[16];
} replay_ctx;

static int collect(const wb_eventlog_rec *rec, const void *data, void *user) {
    (void)data;
    replay_ctx *c = (replay_ctx *)user;
    if (c->count < 16) c->types[c->count] = rec->type;
    c->last_tick = rec->tick;
    c->count++;
    return 0;
}

static int stop_after_2(const wb_eventlog_rec *rec, const void *data, void *user) {
    (void)rec; (void)data;
    int *n = (int *)user;
    return ++(*n) == 2;
}

int main(void) {
    remove(PATH);

    /* ---- create with a snapshot ---- */
    const char *snap = "world-state-v1";
    CHECK(wb_eventlog_create(PATH, snap, (uint32_t)strlen(snap)) == 0, "a log is created");
    wb_eventlog_header h;
    CHECK(wb_eventlog_read_header(PATH, &h) == 0, "the header reads back");
    CHECK(h.magic == WB_EVENTLOG_MAGIC && h.version == WB_EVENTLOG_VERSION, "the magic and version are right");
    CHECK((h.flags & WB_EVENTLOG_HAS_SNAPSHOT) != 0, "the snapshot flag is set");
    CHECK(h.snap_len == (uint32_t)strlen(snap), "the snapshot length is recorded");
    CHECK(h.snap_crc == wb_replay_crc32(snap, (uint32_t)strlen(snap)), "the snapshot checksum is right");
    CHECK(h.seq == 1, "a fresh log is at sequence 1");

    /* ---- read the snapshot back ---- */
    char back[64];
    CHECK(wb_eventlog_read_snapshot(PATH, back, sizeof back) == (long)strlen(snap), "the snapshot reads back");
    CHECK(memcmp(back, snap, strlen(snap)) == 0, "the snapshot bytes are intact");
    CHECK(wb_eventlog_read_snapshot(PATH, back, 4) == -1, "a too-small buffer is refused");

    /* ---- append records ---- */
    CHECK(wb_eventlog_append(PATH, 1, 10, "one", 3) == 0, "the first record is appended");
    CHECK(wb_eventlog_append(PATH, 2, 20, "two", 3) == 0, "the second record is appended");
    CHECK(wb_eventlog_append(PATH, 3, 30, NULL, 0) == 0, "an empty record is appended");
    CHECK(wb_eventlog_read_header(PATH, &h) == 0 && h.last_tick == 3, "the last tick is tracked");

    /* ---- an out-of-order tick is refused ---- */
    CHECK(wb_eventlog_append(PATH, 3, 99, "x", 1) == -1, "a repeated tick is refused");
    CHECK(wb_eventlog_append(PATH, 2, 99, "x", 1) == -1, "a backwards tick is refused");
    CHECK(wb_eventlog_verify(PATH) == 0, "the refused appends wrote nothing");

    /* ---- replay ---- */
    replay_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    CHECK(wb_eventlog_replay(PATH, 100, collect, &ctx) == 3, "every record replays");
    CHECK(ctx.count == 3 && ctx.types[0] == 10 && ctx.types[1] == 20 && ctx.types[2] == 30,
          "the records replay in log order");
    CHECK(ctx.last_tick == 3, "the replay saw the last tick");

    /* ---- replay stops at the requested tick ---- */
    memset(&ctx, 0, sizeof ctx);
    CHECK(wb_eventlog_replay(PATH, 2, collect, &ctx) == 2, "replay stops at until_tick");
    CHECK(ctx.last_tick == 2, "the filtered replay ends at the requested tick");

    /* ---- the callback can stop early ---- */
    int n = 0;
    CHECK(wb_eventlog_replay(PATH, 100, stop_after_2, &n) == 2, "a nonzero callback stops the replay");

    /* ---- verify ---- */
    CHECK(wb_eventlog_verify(PATH) == 0, "an intact log verifies");

    /* ---- a flipped payload byte is caught ---- */
    FILE *f = fopen(PATH, "r+b");
    CHECK(f != NULL, "the log opens for corruption");
    if (f) {
        /* the first record's payload sits at 32 (header) + 12 (snapshot) + 16 (rec header) */
        fseek(f, 32 + 12 + 16, SEEK_SET);
        int c = fgetc(f);
        fseek(f, 32 + 12 + 16, SEEK_SET);
        fputc(c ^ 0xFF, f);
        fclose(f);
    }
    CHECK(wb_eventlog_verify(PATH) == -1, "a flipped payload byte fails verification");
    CHECK(wb_eventlog_replay(PATH, 100, collect, &ctx) == -1, "a corrupt log refuses to replay");

    /* ---- rebuild, then truncate ---- */
    remove(PATH);
    wb_eventlog_create(PATH, snap, (uint32_t)strlen(snap));
    wb_eventlog_append(PATH, 1, 10, "one", 3);
    wb_eventlog_append(PATH, 2, 20, "two", 3);
    CHECK(wb_eventlog_verify(PATH) == 0, "the rebuilt log verifies");
    long snap_end = (long)(sizeof(wb_eventlog_header) + strlen(snap));
    f = fopen(PATH, "r+b");
    if (f) { TEST_TRUNCATE(f, snap_end + 8); fclose(f); }   /* torn record header */
    CHECK(wb_eventlog_verify(PATH) == -1, "a torn record header fails verification");

    f = fopen(PATH, "r+b");
    if (f) { TEST_TRUNCATE(f, snap_end + sizeof(wb_eventlog_rec) + 1); fclose(f); }  /* torn payload */
    CHECK(wb_eventlog_verify(PATH) == -1, "a truncated payload fails verification");

    /* ---- rotation installs a new snapshot and drops the tail ---- */
    remove(PATH);
    wb_eventlog_create(PATH, "snap-A", 6);
    wb_eventlog_append(PATH, 1, 10, "one", 3);
    wb_eventlog_append(PATH, 2, 20, "two", 3);
    CHECK(wb_eventlog_verify(PATH) == 0, "the pre-rotation log verifies");

    const char *snap2 = "snap-B-longer";
    CHECK(wb_eventlog_rotate(PATH, snap2, (uint32_t)strlen(snap2)) == 0, "rotation succeeds");
    CHECK(wb_eventlog_read_header(PATH, &h) == 0 && h.seq == 2, "rotation bumps the sequence");
    CHECK(h.snap_len == (uint32_t)strlen(snap2), "rotation installs the new snapshot length");
    CHECK(h.snap_crc == wb_replay_crc32(snap2, (uint32_t)strlen(snap2)), "rotation installs the new checksum");
    CHECK(h.last_tick == 0, "rotation resets the tick cursor");
    memset(&ctx, 0, sizeof ctx);
    CHECK(wb_eventlog_replay(PATH, 100, collect, &ctx) == 0, "rotation dropped the tail");
    CHECK(wb_eventlog_verify(PATH) == 0, "the rotated log verifies");
    CHECK(wb_eventlog_read_snapshot(PATH, back, sizeof back) == (long)strlen(snap2), "the new snapshot reads back");
    CHECK(memcmp(back, snap2, strlen(snap2)) == 0, "the new snapshot bytes are intact");

    /* appending after a rotation starts from the new tick cursor */
    CHECK(wb_eventlog_append(PATH, 1, 40, "after", 5) == 0, "an append after rotation succeeds");
    CHECK(wb_eventlog_append(PATH, 1, 41, "x", 1) == -1, "the post-rotation tick cursor is enforced");

    /* ---- create without a snapshot ---- */
    remove(PATH);
    CHECK(wb_eventlog_create(PATH, NULL, 0) == 0, "a snapshotless log is created");
    CHECK(wb_eventlog_read_header(PATH, &h) == 0 && (h.flags & WB_EVENTLOG_HAS_SNAPSHOT) == 0,
          "the snapshot flag is clear");
    CHECK(wb_eventlog_read_snapshot(PATH, back, sizeof back) == -1, "there is no snapshot to read");
    CHECK(wb_eventlog_append(PATH, 1, 10, "a", 1) == 0, "a snapshotless log accepts records");

    /* ---- determinism ---- */
    remove(PATH);
    wb_eventlog_create(PATH, "det", 3);
    for (int i = 1; i <= 5; i++) wb_eventlog_append(PATH, (uint32_t)i, (uint32_t)(i * 10), "x", 1);
    wb_eventlog_header d1, d2;
    wb_eventlog_read_header(PATH, &d1);
    remove(PATH);
    wb_eventlog_create(PATH, "det", 3);
    for (int i = 1; i <= 5; i++) wb_eventlog_append(PATH, (uint32_t)i, (uint32_t)(i * 10), "x", 1);
    wb_eventlog_read_header(PATH, &d2);
    CHECK(memcmp(&d1, &d2, sizeof d1) == 0, "a rebuilt log has an identical header");

    /* ---- degenerate input ---- */
    CHECK(wb_eventlog_create(NULL, "x", 1) == -1, "a NULL path is refused");
    CHECK(wb_eventlog_create(PATH, NULL, 4) == -1, "a NULL snapshot with a nonzero length is refused");
    CHECK(wb_eventlog_read_header("build/no_such_log.tmp", &h) == -1, "a missing log is refused");
    CHECK(wb_eventlog_read_header(NULL, &h) == -1, "a NULL path is refused by read_header");
    CHECK(wb_eventlog_read_header(PATH, NULL) == -1, "a NULL header is refused");
    CHECK(wb_eventlog_read_snapshot(NULL, back, sizeof back) == -1, "a NULL path is refused by read_snapshot");
    CHECK(wb_eventlog_replay(NULL, 1, collect, &ctx) == -1, "a NULL path is refused by replay");
    CHECK(wb_eventlog_replay(PATH, 1, NULL, &ctx) == -1, "a NULL callback is refused");
    CHECK(wb_eventlog_verify(NULL) == -1, "a NULL path is refused by verify");
    CHECK(wb_eventlog_verify("build/no_such_log.tmp") == -1, "a missing log does not verify");
    CHECK(wb_eventlog_append(NULL, 1, 1, "x", 1) == -1, "a NULL path is refused by append");
    CHECK(wb_eventlog_rotate(NULL, "x", 1) == -1, "a NULL path is refused by rotate");

    remove(PATH);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
