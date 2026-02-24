// luma_curve_window.h
// SDL2 curve-editor window for the luma curve.
//
// Design goals:
//   - Fully self-contained: include this header and call the functions below.
//   - Readable & easy to modify: each concern (layout, hit-testing, drawing) is
//     a separate small function.
//   - Event handling: SDL has ONE shared event queue for all windows.
//     The main loop drains it, then forwards each event here via
//     CurveWindow_HandleEvent().  CurveWindow_Poll() only does hover
//     detection + conditional redraw — it never calls SDL_PollEvent itself.
//   - Rendering: a dedicated background thread owns SDL_RenderPresent for the
//     curve window.  This means a slow/blocking X11 present (e.g. during a
//     window move) never stalls the main thread.
//
// Public API:
//   void CurveWindow_Open         (LumaCurve&);
//   void CurveWindow_Close        ();
//   bool CurveWindow_IsOpen       ();
//   bool CurveWindow_HandleEvent  (SDL_Event&, LumaCurve&);  // call for every event
//   void CurveWindow_Poll         (LumaCurve&);              // call once per frame

#ifndef LUMA_CURVE_WINDOW_H
#define LUMA_CURVE_WINDOW_H

#include "../common/luma_curve.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <atomic>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------
// Layout constants  (all in pixels, relative to the curve-editor window)
// ---------------------------------------------------------------------------
namespace CWLayout {
    constexpr int WIN_W  = 460;
    constexpr int WIN_H  = 520;

    // The square plot area where the curve lives
    constexpr int PLOT_X = 40;
    constexpr int PLOT_Y = 30;
    constexpr int PLOT_W = 380;
    constexpr int PLOT_H = 380;

    // Control-point handle radius (pixels)
    constexpr int HANDLE_R = 7;

    // "Delete zone" — a point dragged more than this many pixels outside the
    // plot box is removed (except the two anchors).
    constexpr int DELETE_MARGIN = 24;

    // Reset button
    constexpr int BTN_X = PLOT_X;
    constexpr int BTN_Y = PLOT_Y + PLOT_H + 20;
    constexpr int BTN_W = 100;
    constexpr int BTN_H = 28;

    // Enable toggle button
    constexpr int TOG_X = BTN_X + BTN_W + 16;
    constexpr int TOG_Y = BTN_Y;
    constexpr int TOG_W = 140;
    constexpr int TOG_H = BTN_H;
}

// ---------------------------------------------------------------------------
// Colour palette
// ---------------------------------------------------------------------------
namespace CWColor {
    constexpr SDL_Color BG          = {30,  30,  30,  255};
    constexpr SDL_Color PLOT_BG     = {18,  18,  18,  255};
    constexpr SDL_Color GRID        = {55,  55,  55,  255};
    constexpr SDL_Color DIAGONAL    = {60,  60,  60,  255};
    constexpr SDL_Color CURVE       = {220, 180,  60,  255};
    constexpr SDL_Color HANDLE      = {220, 180,  60,  255};
    constexpr SDL_Color HANDLE_HOV  = {255, 220, 100,  255};
    constexpr SDL_Color HANDLE_SEL  = {255, 255, 255,  255};
    constexpr SDL_Color BTN_BG      = {60,  60,  60,  255};
    constexpr SDL_Color BTN_HOV     = {85,  85,  85,  255};
    constexpr SDL_Color BTN_TEXT    = {210, 210, 210,  255};
    constexpr SDL_Color TOG_ON      = {50,  140,  50,  255};
    constexpr SDL_Color TOG_OFF     = {100,  50,  50,  255};
    constexpr SDL_Color LABEL       = {150, 150, 150,  255};
}

// ---------------------------------------------------------------------------
// Internal state — only accessed through the public functions below
// ---------------------------------------------------------------------------
namespace CurveWindowState {
    static SDL_Window*   win      = nullptr;
    static SDL_Renderer* ren      = nullptr;
    static int  dragIdx           = -1;   // index of point being dragged (-1 = none)
    static bool mouseDownInPlot   = false;
    static int  hoverIdx          = -1;   // index of point under the cursor

    // Draw thread: owns SDL_RenderPresent so X11 window-move stalls
    // don't block the main thread.
    static std::thread        drawThread;
    static std::atomic<bool>  drawThreadRunning{false};
    static std::mutex         curveMutex;      // guards curve copy + hoverIdx/dragIdx
    static std::atomic<bool>  redrawRequested{false};
    static LumaCurve          curveCopy;       // snapshot rendered by the draw thread
}

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

// Normalised [0,1] → pixel position in the plot area
inline SDL_Point normToPixel(double nx, double ny) {
    using namespace CWLayout;
    return {
        PLOT_X + (int)std::round(nx * PLOT_W),
        PLOT_Y + (int)std::round((1.0 - ny) * PLOT_H)   // y-axis flipped: 0 = bottom
    };
}

// Pixel position → normalised [0,1] (unclamped, may be outside [0,1])
inline void pixelToNorm(int px, int py, double& nx, double& ny) {
    using namespace CWLayout;
    nx =  (px - PLOT_X) / (double)PLOT_W;
    ny = -(py - PLOT_Y) / (double)PLOT_H + 1.0;
}

// Is the given pixel inside the plot box?
inline bool inPlot(int px, int py) {
    using namespace CWLayout;
    return px >= PLOT_X && px <= PLOT_X + PLOT_W &&
           py >= PLOT_Y && py <= PLOT_Y + PLOT_H;
}

// Is the given pixel inside a rectangle?
inline bool inRect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

// Is the normalised point far enough outside the plot to be deleted?
inline bool isInDeleteZone(int px, int py) {
    using namespace CWLayout;
    int margin = DELETE_MARGIN;
    return px < PLOT_X - margin || px > PLOT_X + PLOT_W + margin ||
           py < PLOT_Y - margin || py > PLOT_Y + PLOT_H + margin;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

static void setColor(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fillRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    setColor(r, c);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void drawRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    setColor(r, c);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderDrawRect(r, &rect);
}

static void drawCircle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c) {
    setColor(r, c);
    // Midpoint circle algorithm
    int x = radius, y = 0, err = 0;
    while (x >= y) {
        SDL_RenderDrawPoint(r, cx+x, cy+y); SDL_RenderDrawPoint(r, cx+y, cy+x);
        SDL_RenderDrawPoint(r, cx-y, cy+x); SDL_RenderDrawPoint(r, cx-x, cy+y);
        SDL_RenderDrawPoint(r, cx-x, cy-y); SDL_RenderDrawPoint(r, cx-y, cy-x);
        SDL_RenderDrawPoint(r, cx+y, cy-x); SDL_RenderDrawPoint(r, cx+x, cy-y);
        y++;
        err += 2*y - 1;
        if (2*err - 2*x + 1 > 0) { x--; err += 1 - 2*x; }
    }
}

static void fillCircle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c) {
    setColor(r, c);
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = (int)std::sqrt((double)(radius*radius - dy*dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

// ---------------------------------------------------------------------------
// Minimal 4×5 pixel bitmap font — uppercase A-Z, digits 0-9, space.
// Each character is encoded as 5 rows of 4 bits (MSB = leftmost pixel).
// Indexed by ASCII value; only printable entries used by drawText are filled.
// Stored as a plain lookup function to stay C++17 compatible.
// ---------------------------------------------------------------------------
static const uint8_t* CW_FontGlyph(unsigned char c) {
    // {row0, row1, row2, row3, row4}
    static const uint8_t tbl[][5] = {
        /* 0  SPC */ {0x00,0x00,0x00,0x00,0x00},
        /* 1  0   */ {0x60,0x90,0x90,0x90,0x60},
        /* 2  1   */ {0x20,0x60,0x20,0x20,0x70},
        /* 3  2   */ {0x60,0x90,0x30,0x40,0xF0},
        /* 4  3   */ {0xE0,0x10,0x60,0x10,0xE0},
        /* 5  4   */ {0x90,0x90,0xF0,0x10,0x10},
        /* 6  5   */ {0xF0,0x80,0xE0,0x10,0xE0},
        /* 7  6   */ {0x60,0x80,0xE0,0x90,0x60},
        /* 8  7   */ {0xF0,0x10,0x20,0x40,0x40},
        /* 9  8   */ {0x60,0x90,0x60,0x90,0x60},
        /* 10 9   */ {0x60,0x90,0x70,0x10,0x60},
        /* 11 A   */ {0x60,0x90,0xF0,0x90,0x90},
        /* 12 B   */ {0xE0,0x90,0xE0,0x90,0xE0},
        /* 13 C   */ {0x70,0x80,0x80,0x80,0x70},
        /* 14 D   */ {0xE0,0x90,0x90,0x90,0xE0},
        /* 15 E   */ {0xF0,0x80,0xE0,0x80,0xF0},
        /* 16 F   */ {0xF0,0x80,0xE0,0x80,0x80},
        /* 17 G   */ {0x70,0x80,0xB0,0x90,0x70},
        /* 18 H   */ {0x90,0x90,0xF0,0x90,0x90},
        /* 19 I   */ {0x70,0x20,0x20,0x20,0x70},
        /* 20 J   */ {0x10,0x10,0x10,0x90,0x60},
        /* 21 K   */ {0x90,0xA0,0xC0,0xA0,0x90},
        /* 22 L   */ {0x80,0x80,0x80,0x80,0xF0},
        /* 23 M   */ {0x90,0xF0,0xF0,0x90,0x90},
        /* 24 N   */ {0x90,0xD0,0xB0,0x90,0x90},
        /* 25 O   */ {0x60,0x90,0x90,0x90,0x60},
        /* 26 P   */ {0xE0,0x90,0xE0,0x80,0x80},
        /* 27 Q   */ {0x60,0x90,0x90,0xB0,0x70},
        /* 28 R   */ {0xE0,0x90,0xE0,0xA0,0x90},
        /* 29 S   */ {0x70,0x80,0x60,0x10,0xE0},
        /* 30 T   */ {0xF0,0x20,0x20,0x20,0x20},
        /* 31 U   */ {0x90,0x90,0x90,0x90,0x60},
        /* 32 V   */ {0x90,0x90,0x90,0x60,0x60},
        /* 33 W   */ {0x90,0x90,0xF0,0xF0,0x90},
        /* 34 X   */ {0x90,0x60,0x60,0x60,0x90},
        /* 35 Y   */ {0x90,0x90,0x60,0x20,0x20},
        /* 36 Z   */ {0xF0,0x10,0x60,0x80,0xF0},
    };
    // Map ASCII → table index
    if (c == ' ')                      return tbl[0];
    if (c >= '0' && c <= '9')          return tbl[1  + (c - '0')];
    if (c >= 'A' && c <= 'Z')          return tbl[11 + (c - 'A')];
    return tbl[0]; // fallback: space
}

// Draw a string using the bitmap font.
// Scale: pixel size of each font pixel (2 = readable 8×10 characters)
static void drawText(SDL_Renderer* r, int x, int y, const char* text, SDL_Color c, int scale = 2) {
    setColor(r, c);
    for (int ci = 0; text[ci]; ++ci) {
        const uint8_t* glyph = CW_FontGlyph((unsigned char)text[ci]);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 4; ++col) {
                if (glyph[row] & (0x80 >> col)) {
                    SDL_Rect px = { x + ci*(4*scale + scale) + col*scale,
                                    y + row*scale,
                                    scale, scale };
                    SDL_RenderFillRect(r, &px);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Main draw routine — renders one frame onto `ren` using the supplied curve
// snapshot.  Called ONLY from the draw thread.
// ---------------------------------------------------------------------------
static void CurveWindow_DrawNow(const LumaCurve& curve) {
    using namespace CWLayout;
    using namespace CWColor;
    using namespace CurveWindowState;

    // Background
    fillRect(ren, 0, 0, WIN_W, WIN_H, BG);

    // ---- Plot background ----
    fillRect(ren, PLOT_X, PLOT_Y, PLOT_W, PLOT_H, PLOT_BG);

    // ---- Grid lines (quarters) ----
    setColor(ren, GRID);
    for (int i = 1; i < 4; ++i) {
        int gx = PLOT_X + PLOT_W * i / 4;
        int gy = PLOT_Y + PLOT_H * i / 4;
        SDL_RenderDrawLine(ren, gx, PLOT_Y, gx, PLOT_Y + PLOT_H);
        SDL_RenderDrawLine(ren, PLOT_X, gy, PLOT_X + PLOT_W, gy);
    }

    // ---- Diagonal identity reference ----
    setColor(ren, DIAGONAL);
    SDL_RenderDrawLine(ren, PLOT_X, PLOT_Y + PLOT_H, PLOT_X + PLOT_W, PLOT_Y);

    // ---- Plot border ----
    drawRect(ren, PLOT_X, PLOT_Y, PLOT_W, PLOT_H, GRID);

    // ---- Axis labels (thin tick marks) ----
    setColor(ren, LABEL);
    for (int i = 0; i <= 4; ++i) {
        // Bottom axis ticks
        int tx = PLOT_X + PLOT_W * i / 4;
        SDL_RenderDrawLine(ren, tx, PLOT_Y + PLOT_H, tx, PLOT_Y + PLOT_H + 5);
        // Left axis ticks
        int ty = PLOT_Y + PLOT_H * i / 4;
        SDL_RenderDrawLine(ren, PLOT_X - 5, ty, PLOT_X, ty);
    }

    // ---- Curve — sample the LUT and draw polyline ----
    setColor(ren, CURVE);
    SDL_Point prev = normToPixel(0.0, curve.lut[0] / 255.0);
    for (int i = 1; i < 256; ++i) {
        SDL_Point cur = normToPixel(i / 255.0, curve.lut[i] / 255.0);
        SDL_RenderDrawLine(ren, prev.x, prev.y, cur.x, cur.y);
        prev = cur;
    }

    // ---- Control point handles ----
    for (int i = 0; i < (int)curve.points.size(); ++i) {
        SDL_Point p = normToPixel(curve.points[i].x, curve.points[i].y);
        SDL_Color col = (i == dragIdx)  ? HANDLE_SEL  :
                        (i == hoverIdx) ? HANDLE_HOV  : HANDLE;
        fillCircle(ren, p.x, p.y, HANDLE_R, col);
        drawCircle(ren, p.x, p.y, HANDLE_R + 1, BG);  // thin dark border
    }

    // ---- Reset button ----
    bool btnHov = false;
    {
        int mx, my; SDL_GetMouseState(&mx, &my);
        btnHov = inRect(mx, my, BTN_X, BTN_Y, BTN_W, BTN_H);
    }
    fillRect(ren, BTN_X, BTN_Y, BTN_W, BTN_H, btnHov ? BTN_HOV : BTN_BG);
    drawRect(ren, BTN_X, BTN_Y, BTN_W, BTN_H, LABEL);
    // Label: "RESET" centred in the button (5 chars × (8+2)px = 50px wide, 10px tall)
    drawText(ren, BTN_X + (BTN_W - 50)/2, BTN_Y + (BTN_H - 10)/2, "RESET", BTN_TEXT, 2);

    // ---- Enable / disable toggle button ----
    {
        SDL_Color togBg = curve.enabled ? TOG_ON : TOG_OFF;
        int mx, my; SDL_GetMouseState(&mx, &my);
        if (inRect(mx, my, TOG_X, TOG_Y, TOG_W, TOG_H)) {
            togBg.r = std::min(255, togBg.r + 30);
            togBg.g = std::min(255, togBg.g + 30);
            togBg.b = std::min(255, togBg.b + 30);
        }
        fillRect(ren, TOG_X, TOG_Y, TOG_W, TOG_H, togBg);
        drawRect(ren, TOG_X, TOG_Y, TOG_W, TOG_H, LABEL);
        // Label: "ON" or "OFF" centred in the button
        const char* togLabel = curve.enabled ? "ON" : "OFF";
        // "ON"=2 chars=20px wide, "OFF"=3 chars=30px wide
        int labelW = curve.enabled ? 20 : 30;
        drawText(ren, TOG_X + (TOG_W - labelW)/2, TOG_Y + (TOG_H - 10)/2, togLabel, BTN_TEXT, 2);
    }

    SDL_RenderPresent(ren);
}

// Request a redraw: snapshot the curve and wake the draw thread.
// Safe to call from the main thread at any time.
static void CurveWindow_Draw(const LumaCurve& curve) {
    using namespace CurveWindowState;
    {
        std::lock_guard<std::mutex> lk(curveMutex);
        curveCopy = curve;
    }
    redrawRequested.store(true);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

inline void CurveWindow_Open(LumaCurve& curve) {
    using namespace CurveWindowState;
    using namespace CWLayout;

    if (win) return;  // already open

    win = SDL_CreateWindow(
        "Luma Curve",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!win) return;

    // SDL_RENDERER_PRESENTVSYNC intentionally omitted — vsync on a secondary
    // window over X11 can block SDL_RenderPresent during window moves.
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) { SDL_DestroyWindow(win); win = nullptr; return; }

    dragIdx         = -1;
    hoverIdx        = -1;
    mouseDownInPlot = false;

    // Take initial snapshot and start the dedicated draw thread.
    // The thread owns SDL_RenderPresent so any X11 stall stays off the main thread.
    {
        std::lock_guard<std::mutex> lk(curveMutex);
        curveCopy = curve;
    }
    redrawRequested.store(true);
    drawThreadRunning.store(true);
    drawThread = std::thread([]() {
        while (drawThreadRunning.load()) {
            if (redrawRequested.exchange(false)) {
                LumaCurve snapshot;
                {
                    std::lock_guard<std::mutex> lk(curveMutex);
                    snapshot = curveCopy;
                }
                if (ren) CurveWindow_DrawNow(snapshot);
            }
            SDL_Delay(8);  // ~120 Hz cap; avoids busy-spin
        }
    });
}

inline void CurveWindow_Close() {
    using namespace CurveWindowState;

    // Stop the draw thread first — before destroying the renderer it uses.
    if (drawThread.joinable()) {
        drawThreadRunning.store(false);
        drawThread.join();
    }

    if (ren) { SDL_DestroyRenderer(ren); ren = nullptr; }
    if (win) { SDL_DestroyWindow(win);  win = nullptr; }
    dragIdx = hoverIdx = -1;
}

inline bool CurveWindow_IsOpen() {
    return CurveWindowState::win != nullptr;
}

// ---------------------------------------------------------------------------
// CurveWindow_HandleEvent — call once for EVERY SDL event in the main loop.
// Returns true if the LUT changed (main view needs a redraw).
// ---------------------------------------------------------------------------
inline bool CurveWindow_HandleEvent(SDL_Event& ev, LumaCurve& curve) {
    using namespace CurveWindowState;
    using namespace CWLayout;

    if (!win) return false;

    uint32_t winID = SDL_GetWindowID(win);
    bool lutChanged = false;
    bool needRedraw = false;

    // Close button on the curve window
    if (ev.type == SDL_WINDOWEVENT &&
        ev.window.windowID == winID &&
        ev.window.event == SDL_WINDOWEVENT_CLOSE) {
        CurveWindow_Close();
        return false;
    }

    // Expose / resize: repaint without changing the curve
    if (ev.type == SDL_WINDOWEVENT &&
        ev.window.windowID == winID &&
        (ev.window.event == SDL_WINDOWEVENT_EXPOSED ||
         ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
        CurveWindow_Draw(curve);
        return false;
    }

    // Only handle mouse events that belong to the curve window
    uint32_t evWinID = 0;
    if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP)
        evWinID = ev.button.windowID;
    else if (ev.type == SDL_MOUSEMOTION)
        evWinID = ev.motion.windowID;

    if (evWinID != 0 && evWinID != winID)
        return false;

    // ---- Mouse down ----
    if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
        int mx = ev.button.x, my = ev.button.y;

        if (inRect(mx, my, BTN_X, BTN_Y, BTN_W, BTN_H)) {
            curve.reset();
            lutChanged = needRedraw = true;
        } else if (inRect(mx, my, TOG_X, TOG_Y, TOG_W, TOG_H)) {
            curve.enabled = !curve.enabled;
            lutChanged = needRedraw = true;
        } else if (inPlot(mx, my)) {
            double nx, ny;
            pixelToNorm(mx, my, nx, ny);
            double radNorm = (HANDLE_R + 4.0) / std::min(PLOT_W, PLOT_H);
            int hit = curve.findNearest(nx, ny, radNorm);
            if (hit >= 0) {
                SDL_Point hp = normToPixel(curve.points[hit].x, curve.points[hit].y);
                int dx = mx - hp.x, dy = my - hp.y;
                if (dx*dx + dy*dy <= (HANDLE_R+4)*(HANDLE_R+4)) {
                    dragIdx = hit;
                    mouseDownInPlot = true;
                    needRedraw = true;
                } else {
                    hit = -1;
                }
            }
            if (hit < 0) {
                // No handle hit → add new point
                dragIdx = curve.addPoint(nx, ny);
                mouseDownInPlot = true;
                curve.buildLUT();
                lutChanged = needRedraw = true;
            }
        }
    }

    // ---- Mouse up ----
    if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
        if (dragIdx >= 0 && mouseDownInPlot) {
            if (isInDeleteZone(ev.button.x, ev.button.y)) {
                curve.removePoint(dragIdx);
                curve.buildLUT();
                lutChanged = needRedraw = true;
            }
        }
        if (dragIdx >= 0) needRedraw = true;
        dragIdx = -1;
        mouseDownInPlot = false;
    }

    // ---- Mouse motion while dragging ----
    if (ev.type == SDL_MOUSEMOTION && dragIdx >= 0) {
        double nx, ny;
        pixelToNorm(ev.motion.x, ev.motion.y, nx, ny);
        curve.movePoint(dragIdx, nx, ny);
        curve.buildLUT();
        lutChanged = needRedraw = true;
    }

    if (needRedraw)
        CurveWindow_Draw(curve);

    return lutChanged;
}

// ---------------------------------------------------------------------------
// CurveWindow_Poll — call once per frame AFTER processing all events.
// Only handles hover highlighting (no SDL_PollEvent — queue already drained).
// ---------------------------------------------------------------------------
inline void CurveWindow_Poll(LumaCurve& curve) {
    using namespace CurveWindowState;
    using namespace CWLayout;

    if (!win || dragIdx >= 0) return;  // skip hover check during active drag

    int mx, my;
    SDL_GetMouseState(&mx, &my);

    double nx, ny;
    pixelToNorm(mx, my, nx, ny);
    double radNorm = (HANDLE_R + 3.0) / std::min(PLOT_W, PLOT_H);
    int newHover = curve.findNearest(nx, ny, radNorm);
    if (newHover >= 0) {
        SDL_Point hp = normToPixel(curve.points[newHover].x, curve.points[newHover].y);
        int dx = mx - hp.x, dy = my - hp.y;
        if (dx*dx + dy*dy > (HANDLE_R+4)*(HANDLE_R+4)) newHover = -1;
    }

    if (newHover != hoverIdx) {
        hoverIdx = newHover;
        CurveWindow_Draw(curve);  // snapshot + signal draw thread
    }
}

#endif // LUMA_CURVE_WINDOW_H
