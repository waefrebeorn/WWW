/*
 * wb_replay_test.c -- LP-47 stone 1 gate: the replay recorder.
 *
 * Devil's-advocate cases: the checksum is the standard CRC-32 (checked against a
 * known vector), a flipped payload byte breaks verification, a flipped length or
 * tick breaks it, a truncated log breaks it, and ticks must be monotonic.
 */
#include "wb_replay.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

int main(void) {
    /* ---- the checksum is the standard CRC-32 (zlib/IEEE vectors) ---- */
    CHECK(wb_replay_crc32("123456789", 9) == 0xCBF43926u, "CRC-32 of the check string is 0xCBF43926");
    CHECK(wb_replay_crc32("a", 1) == 0xE8B7BE43u, "CRC-32 of \"a\" is 0xE8B7BE43");
    CHECK(wb_replay_crc32("", 0) == 0u, "CRC-32 of nothing is zero");
    CHECK(wb_replay_frame_crc(0, NULL, 0) != 0, "a header-only frame still checksums");

    wb_replay r;
    CHECK(wb_replay_init(&r, 8) == 0, "the replay log is created");

    /* ---- recording ---- */
    CHECK(wb_replay_record(&r, 0, "abc", 3) == 0, "the first frame is recorded");
    CHECK(wb_replay_record(&r, 1, "def", 3) == 0, "the second frame is recorded");
    CHECK(wb_replay_record(&r, 2, NULL, 0) == 0, "an empty frame is recorded");
    CHECK(r.n == 3, "three frames are stored");
    CHECK(wb_replay_verify(&r) == 1, "the log verifies");
    CHECK(wb_replay_crc(&r) != 0, "the running total is non-zero");

    /* ---- ticks must be monotonic ---- */
    CHECK(wb_replay_record(&r, 2, "x", 1) == -1, "a repeated tick is refused");
    CHECK(wb_replay_record(&r, 1, "x", 1) == -1, "a backwards tick is refused");
    CHECK(wb_replay_record(&r, 3, "x", 1) == 0, "a forward tick is accepted");

    /* ---- a tampered payload breaks verification ---- */
    uint8_t saved = r.frames[0].data[0];
    r.frames[0].data[0] = (uint8_t)(saved ^ 0xFF);
    CHECK(wb_replay_verify(&r) == 0, "a flipped payload byte breaks verification");
    r.frames[0].data[0] = saved;
    CHECK(wb_replay_verify(&r) == 1, "restoring the byte restores verification");

    /* ---- a tampered length breaks verification ---- */
    r.frames[1].len = 2;
    CHECK(wb_replay_verify(&r) == 0, "a tampered length breaks verification");
    r.frames[1].len = 3;
    CHECK(wb_replay_verify(&r) == 1, "restoring the length restores verification");

    /* ---- a tampered tick breaks verification ---- */
    r.frames[2].tick = 99;
    CHECK(wb_replay_verify(&r) == 0, "a tampered tick breaks verification");
    r.frames[2].tick = 2;
    CHECK(wb_replay_verify(&r) == 1, "restoring the tick restores verification");

    /* ---- a truncated log breaks verification ---- */
    {
        uint32_t saved_n = r.n;
        r.n = saved_n - 1;
        CHECK(wb_replay_verify(&r) == 0, "a truncated log breaks verification");
        r.n = saved_n;
        CHECK(wb_replay_verify(&r) == 1, "restoring the length restores verification");
    }

    /* ---- a payload that does not fit ---- */
    CHECK(wb_replay_record(&r, 4, "01234567890123456789012345678901234567890123456789012345678901234", 65) == -1,
          "an over-long payload is refused");
    CHECK(r.n == 4, "a refused frame is not stored");

    /* ---- a full log refuses to grow ---- */
    wb_replay small;
    wb_replay_init(&small, 2);
    CHECK(wb_replay_record(&small, 0, "a", 1) == 0, "the first slot is taken");
    CHECK(wb_replay_record(&small, 1, "b", 1) == 0, "the second slot is taken");
    CHECK(wb_replay_record(&small, 2, "c", 1) == -1, "a full log refuses to grow");
    CHECK(wb_replay_verify(&small) == 1, "the two stored frames still verify");

    /* ---- clear ---- */
    wb_replay_clear(&r);
    CHECK(r.n == 0 && wb_replay_crc(&r) == 0, "clear empties the log");
    CHECK(wb_replay_verify(&r) == 1, "an empty log verifies");
    CHECK(wb_replay_record(&r, 0, "z", 1) == 0, "a cleared log accepts frames again");

    /* ---- determinism ---- */
    wb_replay a, b;
    wb_replay_init(&a, 8);
    wb_replay_init(&b, 8);
    const char *frames[4] = { "one", "two", "three", "four" };
    for (int i = 0; i < 4; i++) {
        wb_replay_record(&a, (uint32_t)i, frames[i], (uint32_t)strlen(frames[i]));
        wb_replay_record(&b, (uint32_t)i, frames[i], (uint32_t)strlen(frames[i]));
    }
    CHECK(wb_replay_crc(&a) == wb_replay_crc(&b), "the running total is deterministic");
    CHECK(memcmp(a.frames, b.frames, a.n * sizeof a.frames[0]) == 0, "the frames are identical");

    /* ---- degenerate input ---- */
    CHECK(wb_replay_init(NULL, 8) == -1, "a NULL log is rejected");
    CHECK(wb_replay_init(&r, 0) == -1, "a zero capacity is rejected");
    CHECK(wb_replay_record(NULL, 0, "x", 1) == -1, "a NULL log rejects a frame");
    CHECK(wb_replay_verify(NULL) == 0, "a NULL log does not verify");
    CHECK(wb_replay_crc(NULL) == 0, "a NULL log has no total");

    /* ---- file IO ---- */
    /* The round trip is what makes a log portable: record on one build, replay on
     * another. */
    CHECK(wb_replay_save(&r, "build/replay_test.wbr") == 0, "the log is written");
    wb_replay loaded;
    memset(&loaded, 0, sizeof loaded);
    CHECK(wb_replay_load(&loaded, "build/replay_test.wbr") == 0,
          "the log is read back");
    CHECK(loaded.n == r.n, "the frame count survives the round trip");
    CHECK(wb_replay_verify(&loaded) == 1, "the round trip still verifies");
    CHECK(wb_replay_crc(&loaded) == wb_replay_crc(&r),
          "the running total survives the round trip");
    wb_replay_free(&loaded);

    /* A log that is damaged or absent is REFUSED, not replayed: a replay that
     * silently starts from a bad log is worse than no replay. */
    CHECK(wb_replay_load(&loaded, "build/no-such-replay.wbr") == -1,
          "a missing log is refused");
    FILE *g = fopen("build/replay_garbage.wbr", "wb");
    if (g) { fwrite("NOTAREPLAY!", 1, 11, g); fclose(g); }
    CHECK(wb_replay_load(&loaded, "build/replay_garbage.wbr") == -1,
          "a file that is not a replay log is refused");
    CHECK(loaded.n == 0, "a refused load leaves the log empty");
    CHECK(wb_replay_load(NULL, "build/replay_test.wbr") == -1,
          "a NULL log is refused");
    CHECK(wb_replay_load(&loaded, NULL) == -1, "a NULL path is refused");
    CHECK(wb_replay_save(NULL, "build/x.wbr") == -1, "a NULL log saves nothing");
    CHECK(wb_replay_save(&r, NULL) == -1, "a NULL path saves nothing");

    wb_replay_free(&r);
    wb_replay_free(&small);
    wb_replay_free(&a);
    wb_replay_free(&b);
    printf("%d checks, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
