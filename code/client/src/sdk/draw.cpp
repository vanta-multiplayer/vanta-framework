#include "draw.h"

#include "patterns.h"

#include <utils/safe_win32.h>

#include <d3d9.h>
#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace VantaMP::SDK {
    namespace {
        using HudDrawFn = void(__cdecl *)();

        HudDrawFn gOriginalHudDraw = nullptr;
        void (*gOverlay)()         = nullptr;
        IDirect3DDevice9 *gDevice  = nullptr;

        HFONT gTahoma                       = nullptr;
        IDirect3DTexture9 *gTextTexture     = nullptr;
        int gTextTexWidth                   = 0;
        int gTextTexHeight                  = 0;
        bool gLoggedMissingFont             = false;

        void DestroyTextTexture() {
            if (gTextTexture) {
                gTextTexture->Release();
                gTextTexture = nullptr;
            }

            gTextTexWidth  = 0;
            gTextTexHeight = 0;
        }

        void EnsureTahoma() {
            if (gTahoma) {
                return;
            }

            gTahoma = CreateFontA(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, "Tahoma");

            if (!gTahoma && !gLoggedMissingFont) {
                gLoggedMissingFont = true;
                Framework::Logging::GetLogger("Nametag")->error("CreateFontA Tahoma failed; nametag text will be missing");
            }
        }

        int NextPow2(int value) {
            int n = 16;

            while (n < value) {
                n *= 2;
            }

            return n;
        }

        bool EnsureTextTexture(int width, int height) {
            if (!gDevice) {
                return false;
            }

            if (gTextTexture && gTextTexWidth >= width && gTextTexHeight >= height) {
                return true;
            }

            DestroyTextTexture();

            const int texW = NextPow2(width);
            const int texH = NextPow2(height);

            if (FAILED(gDevice->CreateTexture(static_cast<UINT>(texW), static_cast<UINT>(texH), 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &gTextTexture, nullptr))) {
                return false;
            }

            gTextTexWidth  = texW;
            gTextTexHeight = texH;
            return true;
        }

        void RestoreGame2d() {
            if (const auto defined = gPatterns.DefinedState2d) {
                reinterpret_cast<void(__cdecl *)()>(defined)();
            }
        }

        void HudDraw_Hook() {
            if (gOverlay) {
                gOverlay();
            }

            RestoreGame2d();
            gOriginalHudDraw();
        }

        struct RhwVertex {
            float x, y, z, rhw;
            D3DCOLOR color;
        };

        struct RhwTexVertex {
            float x, y, z, rhw;
            D3DCOLOR color;
            float u, v;
        };

        D3DCOLOR WithAlpha(std::uint8_t r, std::uint8_t g, std::uint8_t b, float alpha) {
            const auto a = static_cast<std::uint8_t>(255.0f * alpha);
            return D3DCOLOR_ARGB(a, r, g, b);
        }

        void FillRect(float left, float top, float right, float bottom, D3DCOLOR color) {
            if (!gDevice) {
                return;
            }

            RhwVertex verts[4] = {
                {left, top, 0.0f, 1.0f, color},
                {right, top, 0.0f, 1.0f, color},
                {left, bottom, 0.0f, 1.0f, color},
                {right, bottom, 0.0f, 1.0f, color},
            };

            gDevice->SetTexture(0, nullptr);
            gDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            gDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            gDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            gDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            gDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            gDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
            gDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            gDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(RhwVertex));
        }

        bool RasterizeTahoma(const char *text, int *outW, int *outH) {
            EnsureTahoma();

            if (!gTahoma || !text || !text[0]) {
                return false;
            }

            HDC dc = CreateCompatibleDC(nullptr);

            if (!dc) {
                return false;
            }

            const HFONT oldFont = static_cast<HFONT>(SelectObject(dc, gTahoma));
            SIZE extent {};
            GetTextExtentPoint32A(dc, text, static_cast<int>(std::strlen(text)), &extent);

            const int pad    = 2;
            const int width  = extent.cx + pad * 2;
            const int height = extent.cy + pad * 2;

            if (!EnsureTextTexture(width, height)) {
                SelectObject(dc, oldFont);
                DeleteDC(dc);
                return false;
            }

            BITMAPINFO info {};
            info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth       = width;
            info.bmiHeader.biHeight      = -height;
            info.bmiHeader.biPlanes      = 1;
            info.bmiHeader.biBitCount    = 32;
            info.bmiHeader.biCompression = BI_RGB;

            void *bits         = nullptr;
            const HBITMAP dib  = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            const HBITMAP oldBmp = bits ? static_cast<HBITMAP>(SelectObject(dc, dib)) : nullptr;

            if (!dib || !bits) {
                SelectObject(dc, oldFont);
                DeleteDC(dc);
                return false;
            }

            std::memset(bits, 0, static_cast<size_t>(width * height * 4));
            SetBkMode(dc, TRANSPARENT);

            auto stamp = [&](int ox, int oy, COLORREF color) {
                SetTextColor(dc, color);
                TextOutA(dc, pad + ox, pad + oy, text, static_cast<int>(std::strlen(text)));
            };

            // White stamps into a zeroed DIB: luminance is coverage. Outline
            // first, then the face, so a pixel can be both.
            std::vector<std::uint8_t> outline(static_cast<size_t>(width * height), 0);
            stamp(1, 0, RGB(255, 255, 255));
            stamp(-1, 0, RGB(255, 255, 255));
            stamp(0, 1, RGB(255, 255, 255));
            stamp(0, -1, RGB(255, 255, 255));
            GdiFlush();

            auto *pixels = static_cast<std::uint8_t *>(bits);

            for (int i = 0; i < width * height; ++i) {
                outline[static_cast<size_t>(i)] = pixels[i * 4 + 0];
            }

            std::memset(bits, 0, static_cast<size_t>(width * height * 4));
            stamp(0, 0, RGB(255, 255, 255));
            GdiFlush();

            D3DLOCKED_RECT locked {};

            if (FAILED(gTextTexture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD))) {
                SelectObject(dc, oldBmp);
                DeleteObject(dib);
                SelectObject(dc, oldFont);
                DeleteDC(dc);
                return false;
            }

            auto *dest = static_cast<std::uint8_t *>(locked.pBits);

            for (int y = 0; y < gTextTexHeight; ++y) {
                auto *row = dest + y * locked.Pitch;

                for (int x = 0; x < gTextTexWidth; ++x) {
                    std::uint8_t r = 0, g = 0, b = 0, a = 0;

                    if (x < width && y < height) {
                        const int i          = y * width + x;
                        const std::uint8_t face    = pixels[i * 4 + 0];
                        const std::uint8_t border  = outline[static_cast<size_t>(i)];

                        if (face > 0) {
                            r = g = b = 255;
                            a         = face;
                        }
                        else if (border > 0) {
                            a = border;
                        }
                    }

                    row[x * 4 + 0] = b;
                    row[x * 4 + 1] = g;
                    row[x * 4 + 2] = r;
                    row[x * 4 + 3] = a;
                }
            }

            gTextTexture->UnlockRect(0);

            SelectObject(dc, oldBmp);
            DeleteObject(dib);
            SelectObject(dc, oldFont);
            DeleteDC(dc);

            *outW = width;
            *outH = height;
            return true;
        }

        void DrawTahoma(float x, float y, const char *text, float alpha) {
            int width  = 0;
            int height = 0;

            if (!RasterizeTahoma(text, &width, &height) || !gTextTexture) {
                return;
            }

            const float left   = x - static_cast<float>(width) * 0.5f;
            const float top    = y;
            const float right  = left + static_cast<float>(width);
            const float bottom = top + static_cast<float>(height);
            const float u      = static_cast<float>(width) / static_cast<float>(gTextTexWidth);
            const float v      = static_cast<float>(height) / static_cast<float>(gTextTexHeight);
            const D3DCOLOR tint = WithAlpha(255, 255, 255, alpha);

            RhwTexVertex verts[4] = {
                {left, top, 0.0f, 1.0f, tint, 0.0f, 0.0f},
                {right, top, 0.0f, 1.0f, tint, u, 0.0f},
                {left, bottom, 0.0f, 1.0f, tint, 0.0f, v},
                {right, bottom, 0.0f, 1.0f, tint, u, v},
            };

            gDevice->SetTexture(0, gTextTexture);
            gDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
            gDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            gDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            gDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            gDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            gDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            gDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            gDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            gDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            gDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
            gDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            gDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            gDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            gDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
            gDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            gDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(RhwTexVertex));
            gDevice->SetTexture(0, nullptr);
        }
    } // namespace

    void InstallWorldOverlay(void (*draw)()) {
        gOverlay = draw;

        auto &p = gPatterns;
        gOriginalHudDraw = hook::get_call(reinterpret_cast<HudDrawFn>(p.CHud__DrawCall));
        hook::call(p.CHud__DrawCall, HudDraw_Hook);
    }

    void SetWorldOverlayDevice(IDirect3DDevice9 *device) {
        gDevice = device;
        DestroyTextTexture();
    }

    void OnWorldOverlayDeviceLost() {
        DestroyTextTexture();
    }

    void OnWorldOverlayDeviceReset(IDirect3DDevice9 *device) {
        gDevice = device;
        DestroyTextTexture();
    }

    void DrawNametag(float x, float y, const char *label, float health, float armour, float alpha) {
        if (!gDevice || !label || !label[0] || alpha <= 0.0f) {
            return;
        }

        IDirect3DStateBlock9 *block = nullptr;

        if (FAILED(gDevice->CreateStateBlock(D3DSBT_ALL, &block))) {
            return;
        }

        if (FAILED(block->Capture())) {
            block->Release();
            return;
        }

        DrawTahoma(x, y, label, alpha);

        // GTA HUD bars: black well, then the fill. Armour above health when
        // there is any, same stack the top-right HUD uses.
        constexpr float kBarWidth  = 62.0f;
        constexpr float kBarHeight = 8.0f;
        constexpr float kBorder    = 1.0f;
        constexpr float kGap       = 2.0f;

        float barTop          = y + 18.0f;
        const float barLeft   = x - kBarWidth * 0.5f;

        auto drawBar = [&](float fill, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const float filled = kBarWidth * std::max(0.0f, std::min(fill, 1.0f));

            FillRect(barLeft - kBorder, barTop - kBorder, barLeft + kBarWidth + kBorder, barTop + kBarHeight + kBorder, WithAlpha(0, 0, 0, alpha));
            FillRect(barLeft, barTop, barLeft + kBarWidth, barTop + kBarHeight, WithAlpha(20, 20, 20, alpha));

            if (filled > 0.0f) {
                FillRect(barLeft, barTop, barLeft + filled, barTop + kBarHeight, WithAlpha(r, g, b, alpha));
            }

            barTop += kBarHeight + kGap;
        };

        if (armour > 0.5f) {
            drawBar(std::min(armour, 100.0f) / 100.0f, 225, 225, 225);
        }

        drawBar(std::min(std::max(health, 0.0f), 100.0f) / 100.0f, 180, 25, 29);

        block->Apply();
        block->Release();
    }
} // namespace VantaMP::SDK
