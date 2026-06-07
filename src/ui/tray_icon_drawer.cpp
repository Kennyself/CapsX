/**
 * @file tray_icon_drawer.cpp
 * @brief Tray icon dynamic drawing implementation
 *
 * Uses GDI to draw icons in a memory DC, then converts to HICON.
 *
 * Fix: No black border around the icon. The previous approach used
 * a monochrome mask with a white circle on black background, which
 * creates a black border because the mask says "show pixels in the
 * circle, hide everything else". But the circle edge has anti-aliasing
 * pixels that blend with the black background, creating a visible ring.
 *
 * Solution: Use a fully white mask (entire icon is visible) so no
 * pixels are masked out. The transparent areas in the color bitmap
 * are simply black (RGB 0,0,0), and since the mask is all white,
 * those black pixels are displayed. But for system tray icons on
 * light taskbar backgrounds, we want those areas to be truly transparent.
 *
 * Best approach for tray icons: Use NIF_ICON with an icon that has
 * proper alpha transparency. Windows XP+ supports 32-bit icons with
 * per-pixel alpha. We create a 32-bit ARGB bitmap where:
 *   - Circle area: colored with alpha=255
 *   - Outside circle: alpha=0 (fully transparent)
 */

#include "ui/tray_icon_drawer.h"

HICON create_tray_icon(bool active)
{
    const int ICON_SIZE = GetSystemMetrics(SM_CXSMICON);  // Use system small icon size

    // Create 32-bit ARGB color bitmap
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);

    // Create a 32-bit DIB section for alpha channel support
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ICON_SIZE;
    bmi.bmiHeader.biHeight = -ICON_SIZE;  // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = 0;

    VOID* pBits = nullptr;
    HBITMAP colorBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    ReleaseDC(NULL, screenDC);

    if (pBits == nullptr)
    {
        DeleteObject(colorBmp);
        DeleteDC(memDC);
        return LoadIcon(NULL, IDI_APPLICATION);  // Fallback
    }

    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, colorBmp);

    // Clear to fully transparent (alpha = 0)
    DWORD* pixels = (DWORD*)pBits;
    for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++)
    {
        pixels[i] = 0;  // ARGB: alpha=0, R=0, G=0, B=0
    }

    // Draw colored circle with alpha = 255
    COLORREF bgColor = active ? RGB(46, 89, 163) : RGB(128, 128, 128);
    HBRUSH brush = CreateSolidBrush(bgColor);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, brush);
    Ellipse(memDC, 0, 0, ICON_SIZE, ICON_SIZE);
    SelectObject(memDC, oldBrush);
    DeleteObject(brush);

    // Draw "CX" text
    SetTextColor(memDC, RGB(255, 255, 255));
    SetBkMode(memDC, TRANSPARENT);
    HFONT font = CreateFontW(
        ICON_SIZE * 9 / 16, 0, 0, 0, FW_BLACK, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"
    );
    HFONT oldFont = (HFONT)SelectObject(memDC, font);
    RECT rect = {0, 0, ICON_SIZE, ICON_SIZE};
    DrawTextW(memDC, L"CX", 2, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(memDC, oldFont);
    DeleteObject(font);

    // Now manually set alpha channel:
    // - Circle/text pixels: alpha = 255
    // - Outside circle: alpha = 0 (already 0 from the clear)
    // We need to figure out which pixels are inside the circle.
    // The circle is centered at (ICON_SIZE/2, ICON_SIZE/2) with radius ICON_SIZE/2.
    int cx = ICON_SIZE / 2;
    int cy = ICON_SIZE / 2;
    int r = ICON_SIZE / 2;

    for (int y = 0; y < ICON_SIZE; y++)
    {
        for (int x = 0; x < ICON_SIZE; x++)
        {
            // Check if pixel is inside the circle (with small margin for edge)
            int dx = x - cx;
            int dy = y - cy;
            if (dx * dx + dy * dy <= r * r)
            {
                // Inside circle: set alpha to 255
                // DIB pixels are in BGRA format on Windows
                pixels[y * ICON_SIZE + x] |= 0xFF000000;  // Set alpha byte
            }
            else
            {
                // Outside circle: fully transparent (already 0)
                pixels[y * ICON_SIZE + x] = 0;
            }
        }
    }

    // Create monochrome mask (all white = entire icon visible)
    HDC maskDC = CreateCompatibleDC(NULL);
    HBITMAP maskBmp = CreateCompatibleBitmap(maskDC, ICON_SIZE, ICON_SIZE);
    HBITMAP oldMaskBmp = (HBITMAP)SelectObject(maskDC, maskBmp);
    PatBlt(maskDC, 0, 0, ICON_SIZE, ICON_SIZE, WHITENESS);  // All visible

    // Convert to HICON
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = maskBmp;
    ii.hbmColor = colorBmp;
    HICON icon = CreateIconIndirect(&ii);

    // Cleanup
    SelectObject(memDC, oldBmp);
    SelectObject(maskDC, oldMaskBmp);
    DeleteObject(colorBmp);
    DeleteObject(maskBmp);
    DeleteDC(memDC);
    DeleteDC(maskDC);

    return icon;
}