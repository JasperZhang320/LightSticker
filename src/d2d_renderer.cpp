#include "d2d_renderer.h"

#include <algorithm>

namespace {

ID2D1Factory*   g_d2dFactory    = nullptr;
IDWriteFactory* g_dwriteFactory = nullptr;

template <typename T>
void SafeRelease(T*& p) {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

D2D1_COLOR_F ColorFromRef(COLORREF c, float alpha) {
    return D2D1::ColorF(GetRValue(c) / 255.0f,
                        GetGValue(c) / 255.0f,
                        GetBValue(c) / 255.0f,
                        alpha);
}

bool EnsureTextFormat(StickerRenderer& r, const StickerVisual& vis) {
    if (r.textFormat != nullptr
        && r.cachedFontFace == vis.fontFace
        && r.cachedPtSize == vis.ptSize) {
        return true;
    }
    SafeRelease(r.textFormat);

    // 1 pt = 1/72 in. 1 DIP = 1/96 in. So DIP_size = pt_size * 96/72.
    const float dipSize = vis.ptSize * (96.0f / 72.0f);

    HRESULT hr = g_dwriteFactory->CreateTextFormat(
        vis.fontFace.c_str(),
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        dipSize,
        L"",
        &r.textFormat);
    if (FAILED(hr) || r.textFormat == nullptr) {
        return false;
    }

    r.textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    r.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    r.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    r.cachedFontFace = vis.fontFace;
    r.cachedPtSize   = vis.ptSize;
    return true;
}

}  // namespace

bool D2DInit() {
    if (g_d2dFactory != nullptr && g_dwriteFactory != nullptr) {
        return true;
    }

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory);
    if (FAILED(hr)) {
        return false;
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(&g_dwriteFactory));
    if (FAILED(hr)) {
        SafeRelease(g_d2dFactory);
        return false;
    }
    return true;
}

void D2DShutdown() {
    SafeRelease(g_dwriteFactory);
    SafeRelease(g_d2dFactory);
}

bool D2DCreateRenderer(StickerRenderer& r, HWND hwnd, int dpi) {
    if (g_d2dFactory == nullptr || hwnd == nullptr) {
        return false;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const UINT32 width  = static_cast<UINT32>(std::max<LONG>(1, rc.right  - rc.left));
    const UINT32 height = static_cast<UINT32>(std::max<LONG>(1, rc.bottom - rc.top));

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    rtp.dpiX = static_cast<float>(dpi);
    rtp.dpiY = static_cast<float>(dpi);
    // Use B8G8R8A8 with premultiplied alpha so semi-transparent fills (used
    // by the upcoming design-token themes) composite correctly.
    rtp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                        D2D1_ALPHA_MODE_PREMULTIPLIED);

    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp =
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height));

    HRESULT hr = g_d2dFactory->CreateHwndRenderTarget(rtp, hrtp, &r.rt);
    if (FAILED(hr)) {
        return false;
    }

    r.widthPx  = static_cast<int>(width);
    r.heightPx = static_cast<int>(height);
    r.dpi      = dpi;
    return true;
}

void D2DDestroyRenderer(StickerRenderer& r) {
    SafeRelease(r.textFormat);
    SafeRelease(r.rt);
    r.cachedFontFace.clear();
    r.cachedPtSize = 0.0f;
    r.widthPx = r.heightPx = 0;
}

bool D2DResize(StickerRenderer& r, int widthPx, int heightPx, int dpi) {
    if (r.rt == nullptr) {
        return false;
    }
    if (widthPx <= 0 || heightPx <= 0) {
        return true;
    }
    if (widthPx == r.widthPx && heightPx == r.heightPx && dpi == r.dpi) {
        return true;
    }
    HRESULT hr = r.rt->Resize(D2D1::SizeU(static_cast<UINT32>(widthPx),
                                          static_cast<UINT32>(heightPx)));
    if (FAILED(hr)) {
        return false;
    }
    if (dpi != r.dpi) {
        r.rt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        r.dpi = dpi;
        // Force the text format to be rebuilt at the new DPI on next paint.
        SafeRelease(r.textFormat);
        r.cachedFontFace.clear();
        r.cachedPtSize = 0.0f;
    }
    r.widthPx  = widthPx;
    r.heightPx = heightPx;
    return true;
}

void D2DPaint(StickerRenderer& r, const std::wstring& text, const StickerVisual& vis) {
    if (r.rt == nullptr) {
        return;
    }
    if (!EnsureTextFormat(r, vis)) {
        return;
    }

    // r.widthPx / heightPx are in physical pixels. After SetDpi() Direct2D
    // converts incoming DIP coordinates to physical pixels, so we work in
    // logical (DIP) units throughout.
    const float widthDip  = static_cast<float>(r.widthPx)  * 96.0f / static_cast<float>(r.dpi);
    const float heightDip = static_cast<float>(r.heightPx) * 96.0f / static_cast<float>(r.dpi);
    const float pad       = static_cast<float>(vis.paddingDip);

    r.rt->BeginDraw();
    r.rt->Clear(ColorFromRef(vis.bgColor, 1.0f));

    const D2D1_RECT_F textRect = D2D1::RectF(pad, pad,
                                             widthDip  - pad,
                                             heightDip - pad);

    ID2D1SolidColorBrush* textBrush = nullptr;
    HRESULT hr = r.rt->CreateSolidColorBrush(ColorFromRef(vis.textColor, 1.0f), &textBrush);
    if (SUCCEEDED(hr) && textBrush != nullptr && !text.empty()) {
        r.rt->DrawText(
            text.c_str(),
            static_cast<UINT32>(text.size()),
            r.textFormat,
            textRect,
            textBrush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    SafeRelease(textBrush);

    HRESULT endHr = r.rt->EndDraw();
    if (endHr == D2DERR_RECREATE_TARGET) {
        // GPU device lost (e.g. driver upgrade, mode change). The render
        // target is unusable; tear it down so the next paint will rebuild.
        D2DDestroyRenderer(r);
    }
}
