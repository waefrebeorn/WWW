/*
 * wb_hud.h -- the heads-up display: text and panels drawn into a framebuffer.
 *
 * LP-42 stone 1 of XL. LP-42 asks for a real UI stack: a widget tree, layout,
 * a focus chain, data binding, text shaping and one-draw-call rendering. This
 * stone is deliberately NOT that. It is the part without which none of the rest
 * is visible: putting characters and labelled values on the framebuffer the
 * renderer already owns, so the match's score and clock can be SEEN.
 *
 * What this stone deliberately does not solve, named rather than papered over:
 *
 *   - TEXT SHAPING. The font is a fixed 5x7 bitmap covering ASCII 0x20..0x7E
 *     as uppercase, digits and punctuation. There is no kerning, no ligatures,
 *     no bidirectional text and no non-Latin script. Lowercase input is folded
 *     to uppercase. Everything non-Latin remains blocked on real shaping, which
 *     is LP-42's own hard sub-problem.
 *   - LAYOUT AND WIDGETS. `wb_hud_row` and `wb_hud_match` place fixed rows at
 *     fixed coordinates. There is no container, no flow, no retained tree and no
 *     focus chain -- an immediate-mode stack would be the next stone.
 *
 * Drawing is clipped to the framebuffer on every write, so a caller can never
 * fault by printing past an edge.
 */
#ifndef WB_HUD_H
#define WB_HUD_H

#include <stddef.h>

#include "wb_render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WB_HUD_GLYPH_W 5
#define WB_HUD_GLYPH_H 7
#define WB_HUD_GLYPH_ADV 6      /* 5 pixels + 1 of spacing */

/* Fill an axis-aligned rectangle. Negative sizes draw nothing. */
void wb_hud_rect(wb_framebuffer *fb, int x, int y, int w, int h,
                 uint32_t color);

/* Draw `text` with its top-left at (x, y). Characters outside the framebuffer
 * are clipped; characters outside the font are drawn as a filled box, so a
 * missing glyph is visible rather than silent. Returns the x just past the last
 * glyph drawn. */
int  wb_hud_text(wb_framebuffer *fb, int x, int y, const char *text,
                 uint32_t color, int scale);

/* Width in pixels `wb_hud_text` would occupy. Zero for NULL or empty text. */
int  wb_hud_text_width(const char *text, int scale);

int  wb_hud_line_height(int scale);

/* A label and its value on one row: "SCORE 0-3". The value starts after the
 * label plus one space, so columns line up when the labels are the same length
 * and the caller pads them. */
void wb_hud_row(wb_framebuffer *fb, int x, int y, const char *label,
                const char *value, uint32_t color, int scale);

/* The match status panel: state, score and clock, three rows. `state` is a
 * caller-owned string (the host passes wb_host_objective_state_name's result).
 * This is the whole point of the stone: the objective exists and now it is on
 * the screen. */
void wb_hud_match(wb_framebuffer *fb, int x, int y, const char *state,
                  unsigned score_a, unsigned score_b, double clock_s,
                  uint32_t color, int scale);

/* An onboarding hint, centred along the bottom edge: one line of text over a
 * dark plate so it reads whatever the world is doing behind it. This is the
 * whole of onboarding for now -- a first-time player is told what to do, and the
 * mode's own text comes from its manifest rather than being hardcoded here.
 * An empty or NULL text draws nothing. */
void wb_hud_hint(wb_framebuffer *fb, const char *text, uint32_t fg, int scale);

#ifdef __cplusplus
}
#endif

#endif /* WB_HUD_H */
