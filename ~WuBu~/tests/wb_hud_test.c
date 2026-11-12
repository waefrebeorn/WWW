/*
 * wb_hud_test.c -- verification of the HUD stone.
 *
 * The font is hand-authored bitmaps, so the gate checks the things a typo in the
 * table would break: a glyph renders where it should and nowhere else, the
 * advance is consistent, clipping never writes outside the framebuffer, and the
 * panel composes its rows. A blank frame means the font table is empty; a frame
 * with the wrong pixel lit means a row byte is wrong.
 *
 * The expectations here are derived from the glyph table in wb_hud.c, so a change
 * to a glyph is a change to this gate -- which is the point.
 */
#include "wb_hud.h"

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

static wb_framebuffer fb;
static uint32_t store[320 * 200];
static const uint32_t WHITE = 0xFFFFFF;

static void blank(void) {
    for (int i = 0; i < fb.width * fb.height; i++) fb.pixels[i] = 0;
}

static int lit(void) {
    int n = 0;
    for (int i = 0; i < fb.width * fb.height; i++) if (fb.pixels[i]) n++;
    return n;
}

int main(void) {
    fb.width = 320; fb.height = 200; fb.pixels = store;

    /* ---- a glyph lands where it should ---------------------------------- */
    /* 'A' is 0E 11 11 1F 11 11 11: the top row is two legs, so column 0 of the
     * cell is dark and columns 1..3 are lit. */
    blank();
    wb_hud_text(&fb, 10, 10, "A", WHITE, 1);
    CHECK(lit() > 0, "a glyph lights pixels");
    CHECK(fb.pixels[10 * 320 + 10] == 0, "the A's top-left cell pixel is dark");
    CHECK(fb.pixels[10 * 320 + 11] == WHITE, "the A's leg is lit");
    CHECK(fb.pixels[10 * 320 + 200] == 0, "no stray pixels away from the text");
    /* Row 3 of the glyph is 0x1F: the crossbar is all five columns. */
    int crossbar = 0;
    for (int gx = 0; gx < 5; gx++) if (fb.pixels[13 * 320 + 10 + gx]) crossbar++;
    CHECK(crossbar == 5, "the A's crossbar row is fully lit");
    /* The glyph is 7 rows tall: the eighth row of the cell must be empty. */
    CHECK(fb.pixels[17 * 320 + 10] == 0,
          "the glyph does not spill past its 7 rows");

    /* A space is seven blank rows but still advances. */
    blank();
    int after_space = wb_hud_text(&fb, 0, 0, " ", WHITE, 1);
    CHECK(lit() == 0, "a space draws nothing");
    CHECK(after_space == WB_HUD_GLYPH_ADV, "a space still advances one cell");

    /* ---- the advance and the width agree -------------------------------- */
    CHECK(wb_hud_text_width("ABC", 1) == 3 * WB_HUD_GLYPH_ADV - 1,
          "width counts the glyphs and drops the trailing gap");
    CHECK(wb_hud_text_width("", 1) == 0, "empty text has no width");
    CHECK(wb_hud_text_width(NULL, 1) == 0, "NULL text has no width");
    CHECK(wb_hud_text_width("A", 2) == 2 * WB_HUD_GLYPH_ADV - 2,
          "width scales");

    blank();
    int end = wb_hud_text(&fb, 0, 0, "AB", WHITE, 1);
    CHECK(end == 2 * WB_HUD_GLYPH_ADV,
          "the cursor ends one cell past the last glyph");

    /* ---- scale ---------------------------------------------------------- */
    /* Scale must multiply the ink, not move it: whatever the glyph table says,
     * scale 3 puts nine pixels where scale 1 put one. */
    blank();
    wb_hud_text(&fb, 0, 0, "1", WHITE, 1);
    int ink1 = lit();
    blank();
    wb_hud_text(&fb, 0, 0, "1", WHITE, 3);
    int ink3 = lit();
    CHECK(ink1 > 0, "the glyph has ink at scale 1");
    CHECK(ink3 == 9 * ink1, "scale 3 makes each lit pixel a 3x3 block");

    /* ---- clipping: nothing outside the framebuffer ----------------------- */
    blank();
    wb_hud_text(&fb, 320, 199, "WWW", WHITE, 1);      /* past the corner */
    CHECK(lit() == 0, "text starting past the corner draws nothing");
    wb_hud_text(&fb, -6, -6, "W", WHITE, 1);          /* glyph fully off-screen */
    CHECK(lit() == 0, "a glyph entirely off-screen draws nothing");
    blank();
    wb_hud_text(&fb, 319, 0, "W", WHITE, 1);          /* only column 0 is inside */
    int partial = lit();
    CHECK(partial > 0, "a partially visible glyph still draws its visible part");
    blank();
    wb_hud_text(&fb, 0, 0, "W", WHITE, 1);
    int whole = lit();
    CHECK(partial < whole, "a clipped glyph draws less than the whole glyph");

    blank();
    wb_hud_rect(&fb, -10, -10, 5, 5, WHITE);
    CHECK(lit() == 0, "a rectangle entirely off-screen draws nothing");
    wb_hud_rect(&fb, 0, 0, 0, 5, WHITE);
    CHECK(lit() == 0, "a zero-width rectangle draws nothing");
    wb_hud_rect(&fb, 0, 0, 10, 4, WHITE);
    CHECK(lit() == 40, "a rectangle fills exactly its area");
    blank();
    wb_hud_rect(&fb, 315, 0, 10, 2, WHITE);
    CHECK(lit() == 5 * 2, "a clipped rectangle fills only what is inside");

    /* ---- lowercase is folded, not dropped ------------------------------- */
    blank();
    wb_hud_text(&fb, 0, 0, "a", WHITE, 1);
    int lower = lit();
    blank();
    wb_hud_text(&fb, 0, 0, "A", WHITE, 1);
    int upper = lit();
    CHECK(lower == upper && lower > 0,
          "lowercase folds to the uppercase glyph");

    /* ---- an unknown glyph is a visible box, not nothing ----------------- */
    /* The fallback box is 1F 11 11 11 11 11 1F: 5 + 2*5 + 5 = 20 pixels. */
    blank();
    wb_hud_text(&fb, 0, 0, "\x01", WHITE, 1);
    CHECK(lit() == 20, "an unknown glyph draws its fallback box");

    /* ---- rows and the match panel --------------------------------------- */
    blank();
    wb_hud_row(&fb, 5, 5, "SCORE", "0-3", WHITE, 1);
    CHECK(lit() > 0, "a row draws both its label and its value");
    /* "SCORE" is five glyphs: it ends at 5 + 5*6 = 35, and the value starts one
     * cell later, at 41. '0' is 0E 11 13 15 19 11 0E, so column 1 is lit. */
    CHECK(fb.pixels[5 * 320 + 42] == WHITE,
          "the value is drawn after the label, not on top of it");
    CHECK(fb.pixels[5 * 320 + 41] == 0,
          "the gap between label and value is left dark");

    blank();
    wb_hud_match(&fb, 8, 8, "WON", 0, 3, 117.1, WHITE, 1);
    CHECK(lit() > 0, "the match panel draws over an empty frame");
    int top = -1, bottom = -1;
    for (int y = 0; y < fb.height; y++) {
        int row_has = 0;
        for (int x = 0; x < fb.width; x++)
            if (fb.pixels[y * 320 + x]) { row_has = 1; break; }
        if (row_has) { if (top < 0) top = y; bottom = y; }
    }
    CHECK(top == 8, "the panel starts where it was asked to");
    CHECK(bottom - top >= wb_hud_line_height(1) * 2,
          "the panel spans three rows, not one");

    /* The panel is deterministic: the same inputs give the same frame. */
    static uint32_t frame_a[320 * 200], frame_b[320 * 200];
    blank(); wb_hud_match(&fb, 8, 8, "DRAW", 2, 2, 0.0, WHITE, 1);
    memcpy(frame_a, store, sizeof frame_a);
    blank(); wb_hud_match(&fb, 8, 8, "DRAW", 2, 2, 0.0, WHITE, 1);
    memcpy(frame_b, store, sizeof frame_b);
    CHECK(memcmp(frame_a, frame_b, sizeof frame_a) == 0,
          "the same panel input produces the same pixels");

    /* NULL state must not fault and must draw something. */
    blank();
    wb_hud_match(&fb, 8, 8, NULL, 0, 0, 0.0, WHITE, 1);
    CHECK(lit() > 0, "a NULL state name is handled");

    /* A degenerate framebuffer must be harmless. */
    wb_framebuffer nil = { NULL, 0, 0 };
    wb_hud_text(&nil, 0, 0, "ABC", WHITE, 1);
    wb_hud_rect(&nil, 0, 0, 10, 10, WHITE);
    wb_hud_match(&nil, 0, 0, "X", 1, 1, 1.0, WHITE, 1);
    wb_hud_hint(&nil, "WASD", WHITE, 1);
    CHECK(1, "a NULL framebuffer is harmless");

    /* ---- the hint ------------------------------------------------------- */
    /* Empty and NULL text draw nothing at all. */
    blank();
    wb_hud_hint(&fb, "", WHITE, 1);
    CHECK(lit() == 0, "an empty hint draws nothing");
    wb_hud_hint(&fb, NULL, WHITE, 1);
    CHECK(lit() == 0, "a NULL hint draws nothing");

    /* A hint is centred horizontally and sits on the bottom edge. */
    blank();
    const char *hint = "WASD TO MOVE";
    wb_hud_hint(&fb, hint, WHITE, 1);
    CHECK(lit() > 0, "a hint draws");
    int h_left = fb.width, h_right = -1, h_top = fb.height, h_bottom = -1;
    for (int y = 0; y < fb.height; y++)
        for (int x = 0; x < fb.width; x++)
            if (fb.pixels[y * 320 + x]) {
                if (x < h_left) h_left = x;
                if (x > h_right) h_right = x;
                if (y < h_top) h_top = y;
                if (y > h_bottom) h_bottom = y;
            }
    int ink_w = h_right - h_left + 1;
    int text_w = wb_hud_text_width(hint, 1);
    CHECK(ink_w > text_w, "the hint is drawn over a plate wider than the text");
    CHECK(h_left >= 4 && h_right < fb.width - 4,
          "the plate stays inside the framebuffer");
    /* The hint's text sits at height - line - 12; the plate overhangs it by 3
     * above and 3 below, so its bottom row is 2 past the text's last row. */
    int text_y = fb.height - wb_hud_line_height(1) - 12;
    int text_x = (fb.width - text_w) / 2;
    CHECK(h_bottom == text_y + wb_hud_line_height(1) + 2,
          "the hint sits on the bottom edge with its margin");
    /* Centred: the plate is (width - text_w)/2 - 4 on the left, and the mirror
     * on the right, so the two margins differ by at most one pixel. */
    int plate_l = (fb.width - text_w) / 2 - 4;
    CHECK(h_left == plate_l, "the hint is centred horizontally");

    /* The plate is drawn first and the text on top: the top-left of the text is
     * lit where the plate alone would have been. */
    CHECK(fb.pixels[text_y * 320 + text_x] == WHITE,
          "the text is drawn on top of the plate");

    /* Scale scales the hint too. */
    blank();
    wb_hud_hint(&fb, hint, WHITE, 3);
    int hint3 = lit();
    CHECK(hint3 > 0 && hint3 % 9 == 0, "a scaled hint's ink is a multiple of 9");

    printf("=== HUD TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
