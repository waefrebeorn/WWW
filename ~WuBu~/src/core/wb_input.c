/*
 * wb_input.c -- WuBu Engine input state (slice 10). See wb_input.h.
 */
#include "wb_input.h"

#include <string.h>

void wb_input_init(wb_input *in) {
    memset(in, 0, sizeof *in);
}

void wb_input_key_down(wb_input *in, int code) {
    if (code < 0 || code >= WB_INPUT_MAX_KEYS) return;
    in->keys[code] = 1;
}

void wb_input_key_up(wb_input *in, int code) {
    if (code < 0 || code >= WB_INPUT_MAX_KEYS) return;
    in->keys[code] = 0;
}

void wb_input_mouse_move(wb_input *in, double dx, double dy) {
    in->mouse_dx += dx;
    in->mouse_dy += dy;
}

void wb_input_mouse_pos(wb_input *in, double x, double y) {
    in->mouse_x = x;
    in->mouse_y = y;
}

void wb_input_button(wb_input *in, int button, int down) {
    if (button < 0 || button > 7) return;
    if (down) in->buttons |= (uint8_t)(1u << button);
    else      in->buttons &= (uint8_t)~(1u << button);
}

void wb_input_wheel(wb_input *in, double steps) {
    in->wheel += steps;
}

void wb_input_end_frame(wb_input *in) {
    for (int i = 0; i < WB_INPUT_MAX_KEYS; i++) {
        in->pressed[i]  = (uint8_t)(in->keys[i] && !in->prev[i]);
        in->released[i] = (uint8_t)(!in->keys[i] && in->prev[i]);
        in->prev[i]     = in->keys[i];
    }
    in->mouse_dx = 0.0;
    in->mouse_dy = 0.0;
    in->wheel    = 0.0;
    in->pressed_buttons = (uint8_t)(in->buttons & ~in->prev_buttons);
    in->prev_buttons = in->buttons;
}

int wb_input_down(const wb_input *in, int code) {
    if (code < 0 || code >= WB_INPUT_MAX_KEYS) return 0;
    return in->keys[code] != 0;
}

int wb_input_pressed(const wb_input *in, int code) {
    if (code < 0 || code >= WB_INPUT_MAX_KEYS) return 0;
    return in->pressed[code] != 0;
}

int wb_input_released(const wb_input *in, int code) {
    if (code < 0 || code >= WB_INPUT_MAX_KEYS) return 0;
    return in->released[code] != 0;
}

int wb_input_button_down(const wb_input *in, int button) {
    if (button < 0 || button > 7) return 0;
    return (in->buttons & (1u << button)) != 0;
}

int wb_input_button_pressed(const wb_input *in, int button) {
    if (button < 0 || button > 7) return 0;
    return (in->pressed_buttons & (1u << button)) != 0;
}
