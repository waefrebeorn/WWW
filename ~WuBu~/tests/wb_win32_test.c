/*
 * wb_win32_test.c -- verification of the platform layer.
 *
 * The window is created WITHOUT a title, so it is never shown and never steals
 * focus: this gate runs in a suite, not at a desk. What it proves is the part
 * that matters -- a real HWND with a real DIB section behind it, a framebuffer
 * that renders and presents, keyboard events that reach the input state, and a
 * close that the pump observes.
 *
 * The message injection goes through PostMessage to the real window handle, so
 * the messages travel the same path a real key press does.
 */
#include "wb_win32.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) g_pass++; \
    else { g_fail++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

int main(void) {
    char why[256] = {0};
    wb_input in;
    wb_input_init(&in);

    wb_win32 *w = wb_win32_open(320, 240, NULL, &in, why, sizeof why);
    CHECK(w != NULL, "window opens");
    if (!w) {
        printf("=== WINDOW TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
        printf("reason: %s\n", why);
        return 1;
    }

    CHECK(wb_win32_width(w) == 320 && wb_win32_height(w) == 240,
          "window reports the requested size");
    CHECK(wb_win32_handle(w) != NULL, "window has a native handle");

    wb_framebuffer *fb = wb_win32_framebuffer(w);
    CHECK(fb != NULL && fb->pixels != NULL, "framebuffer exists");
    CHECK(fb && fb->width == 320 && fb->height == 240,
          "framebuffer matches the window");

    /* Render into it and present: the whole point of the layer. */
    wb_render_clear(fb, WB_RENDER_SKY);
    wb_render_camera cam;
    cam.eye[0] = 0.0f; cam.eye[1] = 0.0f; cam.eye[2] = 5.0f;
    cam.at[0] = 0.0f;  cam.at[1] = 0.0f;  cam.at[2] = 0.0f;
    cam.up[0] = 0.0f;  cam.up[1] = 1.0f;  cam.up[2] = 0.0f;
    cam.vfov_deg = 60.0f; cam.near_z = 0.1f; cam.far_z = 100.0f;
    float at_origin[3] = { 0.0f, 0.0f, 0.0f };
    CHECK(wb_render_marker(fb, &cam, at_origin, 1.0f, 0xFFE080) == 1,
          "marker renders into the window's framebuffer");
    CHECK(wb_win32_present(w) == 0, "present succeeds");

    /* The pump keeps a live window alive. */
    CHECK(wb_win32_pump(w) == 1, "live window survives a pump");
    CHECK(!wb_win32_closed(w), "window is not closed yet");

    /* Input: post a real WM_KEYDOWN to the real handle and pump it through. */
    HWND hwnd = (HWND)wb_win32_handle(w);
    PostMessageA(hwnd, WM_KEYDOWN, (WPARAM)'W', 0);
    wb_win32_pump(w);
    CHECK(wb_input_down(&in, 'W') == 1, "a key press reaches the input state");
    PostMessageA(hwnd, WM_KEYUP, (WPARAM)'W', 0);
    wb_win32_pump(w);
    CHECK(wb_input_down(&in, 'W') == 0, "a key release reaches the input state");

    /* Mouse: the button path is the same plumbing as the key path. */
    PostMessageA(hwnd, WM_LBUTTONDOWN, 0, 0);
    wb_win32_pump(w);
    CHECK(wb_input_button_down(&in, 0) == 1, "a mouse button reaches the input state");

    /* Close: the pump must observe it and stop reporting the window as alive. */
    PostMessageA(hwnd, WM_CLOSE, 0, 0);
    CHECK(wb_win32_pump(w) == 0, "closing the window ends the pump");
    CHECK(wb_win32_closed(w) == 1, "closed flag is set");

    wb_win32_close(w);

    /* Failure paths: a bad size is refused rather than crashing. */
    wb_win32 *bad = wb_win32_open(0, 100, NULL, NULL, why, sizeof why);
    CHECK(bad == NULL, "a zero-width window is refused");
    CHECK(why[0] != '\0', "the refusal says why");

    printf("=== WINDOW TESTS: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
