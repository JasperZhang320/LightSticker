// Direct2D + DirectWrite renderer for a single sticker window.
//
// Each StickerRenderer owns:
//   - one ID2D1HwndRenderTarget bound to a sticker HWND
//   - one IDWriteTextFormat cached on (font face, point size)
//
// The two factories are process-global and created once via D2DInit().
// Owners must call D2DDestroyRenderer() before the HWND is destroyed.

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <string>

// Pure-data description of how a sticker should be drawn. main.cpp builds
// this from a StickerState (theme + custom overrides) right before painting.
struct StickerVisual {
    std::wstring fontFace = L"Segoe UI";
    float        ptSize        = 13.0f;
    COLORREF     bgColor       = RGB(255, 248, 176);
    COLORREF     textColor     = RGB(20, 20, 20);
    float        cornerRadius  = 12.0f;   // corner radius in DIPs (logical px)
    int          paddingDip    = 12;
};

struct StickerRenderer {
    ID2D1HwndRenderTarget* rt          = nullptr;
    IDWriteTextFormat*     textFormat  = nullptr;

    int           widthPx           = 0;
    int           heightPx          = 0;
    int           dpi               = 96;
    std::wstring  cachedFontFace;
    float         cachedPtSize      = 0.0f;
};

bool D2DInit();
void D2DShutdown();

// Creates an HwndRenderTarget bound to `hwnd`. Returns false on failure;
// in that case the caller falls back to GDI painting.
bool D2DCreateRenderer(StickerRenderer& r, HWND hwnd, int dpi);
void D2DDestroyRenderer(StickerRenderer& r);

// Resizes the underlying render target. Cheap; safe to call on every WM_SIZE.
bool D2DResize(StickerRenderer& r, int widthPx, int heightPx, int dpi);

// Paints the sticker. Caller must invalidate / validate the window separately
// (Direct2D does not interact with the GDI dirty region).
void D2DPaint(StickerRenderer& r, const std::wstring& text, const StickerVisual& vis);
