/*
 * wb_win32.h -- the platform layer: a window, a framebuffer, and input.
 *
 * The engine is platform-agnostic everywhere else; this is the one file that
 * knows what an OS is. It gives the host three things:
 *
 *   - a window, sized so its CLIENT area is exactly the framebuffer;
 *   - presentation of an XRGB8888 framebuffer into that client area (GDI);
 *   - the window's keyboard/mouse events fed into a wb_input, so the same
 *     input -> controller -> velocity path the headless binary drives with
 *     `--input` is driven here by a real person at a keyboard.
 *
 * Only Win32 and GDI: no GLFW, no SDL, no external SDK. MinGW ships the headers
 * and import libs, and the window is ~200 lines.
 *
 * WHY THIS EXISTS: until now the binary could render a frame but had nowhere to
 * put it. `wb_render` works on "any framebuffer (VBE on metal, window on the
 * rig)" -- this is the window on the rig.
 */
#ifndef WB_WIN32_H
#define WB_WIN32_H

#include <stdint.h>

#include "wb_input.h"
#include "wb_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wb_win32 wb_win32;

/* Open a window whose client area is `w` x `h` pixels, with a backing
 * framebuffer of the same size. Keyboard and mouse events go straight into
 * `in` (which may be NULL). Returns NULL on failure; `err` (if not NULL)
 * receives the reason.
 *
 * The window is created hidden-capable but shown immediately; a caller that
 * wants to open it without stealing focus can pass title == NULL, which skips
 * ShowWindow (used by the headless-ish smoke test). */
wb_win32 *wb_win32_open(int w, int h, const char *title, wb_input *in,
                        char *err, size_t err_sz);

void wb_win32_close(wb_win32 *win);

/* The window's own framebuffer: render into it, then present it. */
wb_framebuffer *wb_win32_framebuffer(wb_win32 *win);

/* Copy the window's framebuffer to its client area. Returns 0 on success. */
int wb_win32_present(wb_win32 *win);

/* Pump the window's messages, feeding input as they arrive. Returns 1 while the
 * window is alive, 0 once the user has closed it. */
int wb_win32_pump(wb_win32 *win);

/* 1 once the window has been closed (WM_CLOSE seen), 0 otherwise. */
int wb_win32_closed(const wb_win32 *win);

int wb_win32_width(const wb_win32 *win);
int wb_win32_height(const wb_win32 *win);

/* The native window handle (HWND on Win32), as an opaque pointer. A caller uses
 * this to post a message to the window -- the gate uses it to inject a key press
 * and to close the window, which is otherwise only reachable from a real
 * keyboard and a title-bar button. */
void *wb_win32_handle(const wb_win32 *win);

const char *wb_win32_error(const wb_win32 *win);

#ifdef __cplusplus
}
#endif

#endif /* WB_WIN32_H */
