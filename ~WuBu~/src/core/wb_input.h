/*
 * wb_input.h -- WuBu Engine input state (slice 10).
 *
 * Platform-agnostic input core: a key array with edge detection plus mouse
 * deltas. The platform layer (window, gamepad, touch) feeds it; game logic
 * reads it. Kept free of OS headers so it is unit-testable headlessly -- the
 * windowed backend arrives with the GPU slice.
 */
#ifndef WB_INPUT_H
#define WB_INPUT_H

#include <stdint.h>

#define WB_INPUT_MAX_KEYS 256

typedef struct {
    uint8_t  keys[WB_INPUT_MAX_KEYS];      /* held */
    uint8_t  prev[WB_INPUT_MAX_KEYS];      /* held last frame */
    uint8_t  pressed[WB_INPUT_MAX_KEYS];   /* went down this frame */
    uint8_t  released[WB_INPUT_MAX_KEYS];  /* went up this frame */

    double   mouse_dx, mouse_dy;           /* motion since last frame */
    double   mouse_x,  mouse_y;            /* absolute position */
    uint8_t  buttons;                      /* bit0 L, bit1 R, bit2 M */
    uint8_t  prev_buttons;
    uint8_t  pressed_buttons;              /* went down this frame */

    double   wheel;                        /* wheel steps this frame */
} wb_input;

void wb_input_init(wb_input *in);

/* Platform layer calls these. `code` is a platform key code (0..255). */
void wb_input_key_down(wb_input *in, int code);
void wb_input_key_up(wb_input *in, int code);

/* Accumulate mouse motion (pixels or normalised units, caller's choice). */
void wb_input_mouse_move(wb_input *in, double dx, double dy);
void wb_input_mouse_pos(wb_input *in, double x, double y);
void wb_input_button(wb_input *in, int button, int down);
void wb_input_wheel(wb_input *in, double steps);

/* Call once at the end of each frame: prev <- keys, clears edges and deltas. */
void wb_input_end_frame(wb_input *in);

/* Queries. */
int  wb_input_down(const wb_input *in, int code);
int  wb_input_pressed(const wb_input *in, int code);
int  wb_input_released(const wb_input *in, int code);
int  wb_input_button_down(const wb_input *in, int button);
int  wb_input_button_pressed(const wb_input *in, int button);

#endif /* WB_INPUT_H */
