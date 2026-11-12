/*
 * wb_hud.c -- implementation. See wb_hud.h for the scope and the limits.
 *
 * The font is 7 bytes per glyph, one byte per row, bit 4 leftmost. Glyphs are
 * stored only for the characters this stone actually supports (digits, A-Z,
 * punctuation); everything else falls back to a box so a missing glyph is a
 * visible defect rather than an invisible one.
 */
#include "wb_hud.h"

#include <stdio.h>

/* ---- the font ------------------------------------------------------------ */

typedef struct { char ch; unsigned char rows[WB_HUD_GLYPH_H]; } wb_hud_glyph;

static const wb_hud_glyph kFont[] = {
    { ' ',  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '!',  { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
    { '(',  { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
    { ')',  { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
    { ',',  { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
    { '-',  { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '.',  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '/',  { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
    { '0',  { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1',  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2',  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3',  { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4',  { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5',  { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6',  { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7',  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8',  { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9',  { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { ':',  { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } },
    { '=',  { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
    { '?',  { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } },
    { 'A',  { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B',  { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C',  { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D',  { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
    { 'E',  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F',  { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G',  { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H',  { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I',  { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J',  { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K',  { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L',  { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M',  { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N',  { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O',  { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P',  { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q',  { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R',  { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S',  { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T',  { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U',  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V',  { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W',  { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
    { 'X',  { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y',  { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z',  { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '_',  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
};

/* A filled box: the visible "this glyph is not in the font" mark. */
static const unsigned char kBox[WB_HUD_GLYPH_H] =
    { 0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F };

static const unsigned char *glyph_for(char c) {
    /* Uppercase-only font: lowercase is folded, never dropped. */
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (size_t i = 0; i < sizeof kFont / sizeof kFont[0]; i++)
        if (kFont[i].ch == c) return kFont[i].rows;
    return kBox;
}

/* ---- primitives ---------------------------------------------------------- */

static void hud_pixel(wb_framebuffer *fb, int x, int y, uint32_t color) {
    /* Every write is clipped: a caller must never be able to fault by printing
     * past an edge of the framebuffer. */
    if (!fb || !fb->pixels) return;
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) return;
    fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x] = color;
}

void wb_hud_rect(wb_framebuffer *fb, int x, int y, int w, int h,
                 uint32_t color) {
    if (!fb || w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            hud_pixel(fb, x + col, y + row, color);
}

int wb_hud_text(wb_framebuffer *fb, int x, int y, const char *text,
                uint32_t color, int scale) {
    if (!text) return x;
    if (scale < 1) scale = 1;

    int cx = x;
    for (const char *p = text; *p; p++) {
        const unsigned char *rows = glyph_for(*p);
        for (int gy = 0; gy < WB_HUD_GLYPH_H; gy++) {
            unsigned char bits = rows[gy];
            for (int gx = 0; gx < WB_HUD_GLYPH_W; gx++) {
                if (!(bits & (0x10 >> gx))) continue;
                /* One glyph pixel becomes a scale x scale block. */
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        hud_pixel(fb, cx + gx * scale + sx,
                                  y + gy * scale + sy, color);
            }
        }
        cx += WB_HUD_GLYPH_ADV * scale;
    }
    return cx;
}

int wb_hud_text_width(const char *text, int scale) {
    if (!text || scale < 1) return 0;
    int n = 0;
    for (const char *p = text; *p; p++) n++;
    return n ? n * WB_HUD_GLYPH_ADV * scale - scale : 0;
}

int wb_hud_line_height(int scale) {
    return (scale < 1 ? 1 : scale) * WB_HUD_GLYPH_H;
}

void wb_hud_row(wb_framebuffer *fb, int x, int y, const char *label,
                const char *value, uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    if (!label) label = "";
    int after_label = wb_hud_text(fb, x, y, label, color, scale);
    /* One space between the label and the value. */
    int value_x = after_label + WB_HUD_GLYPH_ADV * scale;
    if (value) wb_hud_text(fb, value_x, y, value, color, scale);
}

void wb_hud_match(wb_framebuffer *fb, int x, int y, const char *state,
                  unsigned score_a, unsigned score_b, double clock_s,
                  uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    int lh = wb_hud_line_height(scale) + scale;   /* a blank pixel between rows */

    char score[32], clock_s_buf[32];
    snprintf(score, sizeof score, "%u-%u", score_a, score_b);
    if (clock_s > 0.0) snprintf(clock_s_buf, sizeof clock_s_buf, "%.1f", clock_s);
    else               snprintf(clock_s_buf, sizeof clock_s_buf, "0.0");

    wb_hud_row(fb, x, y,                       "STATE", state ? state : "-", color, scale);
    wb_hud_row(fb, x, y + lh,                 "SCORE", score,               color, scale);
    wb_hud_row(fb, x, y + lh * 2,             "CLOCK", clock_s_buf,         color, scale);
}

void wb_hud_hint(wb_framebuffer *fb, const char *text, uint32_t fg, int scale) {
    if (!fb || !fb->pixels || !text || !text[0]) return;
    if (scale < 1) scale = 1;
    int w = wb_hud_text_width(text, scale);
    int h = wb_hud_line_height(scale);
    if (w <= 0 || fb->width <= 0 || fb->height <= 0) return;

    int x = (fb->width - w) / 2;
    int y = fb->height - h - 12;
    /* The plate, then the text on top: the hint must read over any world. */
    wb_hud_rect(fb, x - 4, y - 3, w + 8, h + 6, 0x101418);
    wb_hud_text(fb, x, y, text, fg, scale);
}
