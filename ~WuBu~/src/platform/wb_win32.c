/*
 * wb_win32.c -- Win32/GDI implementation of the platform layer. See wb_win32.h.
 *
 * Presentation is a DIB section blitted with StretchBlt: the framebuffer is
 * XRGB8888 with row 0 at the TOP, which is a top-down DIB (negative biHeight),
 * so no row flipping is needed anywhere.
 *
 * The window procedure feeds a wb_input directly, which is why the platform
 * layer can be ~200 lines: the input module already knows what a key array is.
 */
#include "wb_win32.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

struct wb_win32 {
    HWND    hwnd;
    HDC     mdc;              /* memory DC holding the DIB */
    HBITMAP dib;              /* the DIB section */
    HGDIOBJ old_dib;          /* what was in the DC before our DIB */
    uint32_t *pixels;         /* the DIB's bits == the framebuffer */
    int     w, h;
    int     closed;
    wb_input *in;             /* where events go; may be NULL */
    char    err[256];
};

static LRESULT CALLBACK wb_win32_wndproc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    wb_win32 *win = (wb_win32 *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CLOSE:
        if (win) win->closed = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        /* The host's default key map is WASD + Space + Shift, which are exactly
         * the Windows virtual-key codes, so the code passes through unchanged. */
        if (win && win->in && wp < WB_INPUT_MAX_KEYS && !(lp & 0x40000000))
            wb_input_key_down(win->in, (int)wp);
        return 0;
    case WM_KEYUP:
        if (win && win->in && wp < WB_INPUT_MAX_KEYS)
            wb_input_key_up(win->in, (int)wp);
        return 0;

    case WM_MOUSEMOVE:
        if (win && win->in) {
            /* deltas from the previous position the module last saw */
            static int px = -1, py = -1;
            int x = (int)(short)LOWORD(lp), y = (int)(short)HIWORD(lp);
            if (px >= 0) {
                wb_input_mouse_move(win->in, (double)(x - px), (double)(y - py));
            }
            px = x; py = y;
        }
        return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        if (win && win->in)
            wb_input_button(win->in, 0, msg == WM_LBUTTONDOWN);
        return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        if (win && win->in)
            wb_input_button(win->in, 1, msg == WM_RBUTTONDOWN);
        return 0;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        if (win && win->in)
            wb_input_button(win->in, 2, msg == WM_MBUTTONDOWN);
        return 0;
    case WM_MOUSEWHEEL:
        if (win && win->in)
            wb_input_wheel(win->in, (double)(short)HIWORD(wp) / 120.0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

wb_win32 *wb_win32_open(int w, int h, const char *title, wb_input *in,
                        char *err, size_t err_sz) {
    if (w <= 0 || h <= 0) {
        if (err) snprintf(err, err_sz, "a window needs a positive size");
        return NULL;
    }

    wb_win32 *win = (wb_win32 *)calloc(1, sizeof *win);
    if (!win) {
        if (err) snprintf(err, err_sz, "out of memory");
        return NULL;
    }
    win->w = w; win->h = h; win->in = in;

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = wb_win32_wndproc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "WuBuEngineWindow";
    wc.hIcon         = LoadIconA(NULL, IDI_APPLICATION);
    if (!RegisterClassExA(&wc)) {
        if (err) snprintf(err, err_sz, "cannot register the window class");
        free(win);
        return NULL;
    }

    /* Size the window so the CLIENT area is exactly w x h. */
    RECT r = { 0, 0, w, h };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    win->hwnd = CreateWindowExA(
        0, "WuBuEngineWindow", title ? title : "WuBu Engine",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!win->hwnd) {
        if (err) snprintf(err, err_sz, "cannot create the window");
        free(win);
        return NULL;
    }
    SetWindowLongPtrA(win->hwnd, GWLP_USERDATA, (LONG_PTR)win);

    /* The DIB section IS the framebuffer: a top-down 32-bpp XRGB grid. */
    HDC screen = GetDC(win->hwnd);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;        /* negative == top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    win->dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!win->dib) {
        if (err) snprintf(err, err_sz, "cannot create the framebuffer");
        ReleaseDC(win->hwnd, screen);
        DestroyWindow(win->hwnd);
        free(win);
        return NULL;
    }
    win->pixels = (uint32_t *)bits;
    win->mdc = CreateCompatibleDC(screen);
    win->old_dib = SelectObject(win->mdc, win->dib);
    ReleaseDC(win->hwnd, screen);

    /* A NULL title means the caller wants the window created but not shown. */
    if (title) {
        ShowWindow(win->hwnd, SW_SHOW);
        UpdateWindow(win->hwnd);
    }
    return win;
}

void wb_win32_close(wb_win32 *win) {
    if (!win) return;
    if (win->mdc) {
        SelectObject(win->mdc, win->old_dib);
        DeleteDC(win->mdc);
    }
    if (win->dib) DeleteObject(win->dib);
    if (win->hwnd) DestroyWindow(win->hwnd);
    free(win);
}

wb_framebuffer *wb_win32_framebuffer(wb_win32 *win) {
    static wb_framebuffer fb;      /* the window owns exactly one */
    if (!win) return NULL;
    fb.pixels = win->pixels;
    fb.width  = win->w;
    fb.height = win->h;
    return &fb;
}

int wb_win32_present(wb_win32 *win) {
    if (!win || !win->hwnd || !win->mdc) return -1;
    HDC dc = GetDC(win->hwnd);
    if (!dc) return -1;
    int ok = StretchBlt(dc, 0, 0, win->w, win->h,
                        win->mdc, 0, 0, win->w, win->h, SRCCOPY);
    ReleaseDC(win->hwnd, dc);
    return ok ? 0 : -1;
}

int wb_win32_pump(wb_win32 *win) {
    if (!win) return 0;
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            win->closed = 1;
            return 0;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return win->closed ? 0 : 1;
}

int wb_win32_closed(const wb_win32 *win) { return win ? win->closed : 1; }

int wb_win32_width(const wb_win32 *win)  { return win ? win->w : 0; }
int wb_win32_height(const wb_win32 *win) { return win ? win->h : 0; }

void *wb_win32_handle(const wb_win32 *win) { return win ? (void *)win->hwnd : NULL; }

const char *wb_win32_error(const wb_win32 *win) {
    return (win && win->err[0]) ? win->err : NULL;
}
