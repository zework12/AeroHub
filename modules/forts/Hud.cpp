#include "Hud.hpp"

#include <algorithm>
#include <cmath>

#include "Ui.hpp"

namespace forts {
namespace {

using namespace aero::ui;

Gdiplus::Font MakeFont(float px, float scale, int style) { return Gdiplus::Font(L"Segoe UI", px * scale, style, Gdiplus::UnitPixel); }

}  // namespace

void RenderLabel(const std::wstring& text, COLORREF accent, float scale, HudImage& out) {
    const float s = scale;
    const Gdiplus::Font font = MakeFont(12, s, Gdiplus::FontStyleBold);
    // Measure on a throwaway bitmap.
    Gdiplus::Bitmap probe(4, 4, PixelFormat32bppPARGB);
    Gdiplus::Graphics pg(&probe);
    pg.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    const float textW = MeasureW(pg, text, font);
    out.w = std::max(8, static_cast<int>(textW + 16 * s + 0.5f));
    out.h = std::max(8, static_cast<int>(20 * s + 0.5f));
    out.px.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h), 0u);
    Gdiplus::Bitmap bmp(out.w, out.h, out.w * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(out.px.data()));
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));
    const RectF r(0, 0, static_cast<float>(out.w), static_cast<float>(out.h));
    FillRR(g, r, 7 * s, kBg, 226);
    StrokeRR(g, r, 7 * s, accent, 1.5f);
    DrawStr(g, text, font, r, kText, Gdiplus::StringAlignmentCenter);
}

void RenderBaseView(const std::vector<ViewSegment>& segments, const std::vector<ViewDevice>& devices, float scale, HudImage& out) {
    const float s = scale;
    out.w = static_cast<int>(480 * s + 0.5f);
    out.h = static_cast<int>(350 * s + 0.5f);
    out.px.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h), 0u);
    Gdiplus::Bitmap bmp(out.w, out.h, out.w * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(out.px.data()));
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));

    const RectF card(0, 0, static_cast<float>(out.w), static_cast<float>(out.h));
    FillRR(g, card, 12 * s, kSurface, 242);
    StrokeRR(g, card, 12 * s, kBorder);
    const Gdiplus::Font fH2 = MakeFont(14, s, Gdiplus::FontStyleBold), fSmall = MakeFont(11.5f, s, Gdiplus::FontStyleRegular),
                        fTiny = MakeFont(10.5f, s, Gdiplus::FontStyleBold);

    int guns = 0;
    for (const ViewDevice& d : devices) guns += d.kind == 1 ? 1 : 0;
    DrawStr(g, L"Observer", fH2, RectF(14 * s, 8 * s, 120 * s, 24 * s), kText);
    DrawStr(g, L"enemy base", fSmall, RectF(88 * s, 9 * s, 120 * s, 24 * s), kDim);
    const float liveW = DrawPill(g, s, fSmall, out.w - 14 * s - 70 * s, 7 * s, L"LIVE", kBad);
    (void)liveW;

    const RectF plot(10 * s, 40 * s, out.w - 20 * s, out.h - 66 * s);
    FillRR(g, plot, 8 * s, kBg, 245);
    g.SetClip(plot);

    // Faint grid so the picture reads as a camera view.
    {
        Gdiplus::Pen grid(Col(kBorder, 70), 1.0f);
        for (float x = plot.X + 40 * s; x < plot.X + plot.Width; x += 40 * s) g.DrawLine(&grid, x, plot.Y, x, plot.Y + plot.Height);
        for (float y = plot.Y + 40 * s; y < plot.Y + plot.Height; y += 40 * s) g.DrawLine(&grid, plot.X, y, plot.X + plot.Width, y);
    }

    if (segments.empty() && devices.empty()) {
        g.ResetClip();
        DrawStr(g, L"No enemy base in sight", fSmall, plot, kDim, Gdiplus::StringAlignmentCenter);
        return;
    }
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
    auto grow = [&](float x, float y) {
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    };
    for (const ViewSegment& sg : segments) { grow(sg.x1, sg.y1); grow(sg.x2, sg.y2); }
    for (const ViewDevice& d : devices) grow(d.x, d.y);
    const float spanX = std::max(800.0f, maxX - minX), spanY = std::max(500.0f, maxY - minY);
    const float k = 0.90f * std::min(plot.Width / spanX, plot.Height / spanY);  // zoom: the whole base always fits
    const float cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    auto toX = [&](float wx) { return plot.X + plot.Width / 2 + (wx - cx) * k; };
    auto toY = [&](float wy) { return plot.Y + plot.Height / 2 + (wy - cy) * k; };

    // Beams: warm wood tone when healthy, yellow / red as they get damaged; unfinished beams are faint.
    for (const ViewSegment& sg : segments) {
        const COLORREF c = sg.health > 0.66f ? RGB(196, 142, 92) : (sg.health > 0.33f ? kWarn : kBad);
        Gdiplus::Pen pen(Col(c, sg.building ? 90 : 230), std::max(1.0f, 1.5f * s));
        g.DrawLine(&pen, toX(sg.x1), toY(sg.y1), toX(sg.x2), toY(sg.y2));
    }
    // Devices on top. Labels only for guns and the reactor, and only while the base is small enough to read them.
    const bool labels = guns <= 16;
    for (const ViewDevice& d : devices) {
        const float x = toX(d.x), y = toY(d.y);
        if (d.kind == 2) {
            const float r = 7 * s;
            const Gdiplus::PointF diamond[4] = {{x, y - r}, {x + r, y}, {x, y + r}, {x - r, y}};
            Gdiplus::SolidBrush b(Col(kWarn));
            g.FillPolygon(&b, diamond, 4);
            DrawStr(g, L"reactor", fTiny, RectF(x + r + 2 * s, y - 8 * s, 70 * s, 16 * s), kWarn);
        } else if (d.kind == 1) {
            FillRR(g, RectF(x - 4.5f * s, y - 4.5f * s, 9 * s, 9 * s), 2 * s, kBad);
            if (labels && !d.label.empty())
                DrawStr(g, std::wstring(d.label.begin(), d.label.end()), fTiny, RectF(x + 7 * s, y - 8 * s, 90 * s, 16 * s), kText);
        } else {
            FillRR(g, RectF(x - 2 * s, y - 2 * s, 4 * s, 4 * s), 1 * s, RGB(160, 170, 190), 200);
        }
    }
    g.ResetClip();

    // Viewfinder corners and footer.
    {
        Gdiplus::Pen br(Col(kText, 110), 1.6f * s);
        const float L = 14 * s, in = 5 * s;
        const float x0 = plot.X + in, y0 = plot.Y + in, x1 = plot.X + plot.Width - in, y1 = plot.Y + plot.Height - in;
        g.DrawLine(&br, x0, y0, x0 + L, y0); g.DrawLine(&br, x0, y0, x0, y0 + L);
        g.DrawLine(&br, x1, y0, x1 - L, y0); g.DrawLine(&br, x1, y0, x1, y0 + L);
        g.DrawLine(&br, x0, y1, x0 + L, y1); g.DrawLine(&br, x0, y1, x0, y1 + L);
        g.DrawLine(&br, x1, y1, x1 - L, y1); g.DrawLine(&br, x1, y1, x1, y1 + L);
    }
    wchar_t left[96], right[96];
    swprintf_s(left, L"view %.1fk x %.1fk units", spanX / 1000.0f, spanY / 1000.0f);
    swprintf_s(right, L"%d guns  |  %d devices  |  %d beams", guns, static_cast<int>(devices.size()), static_cast<int>(segments.size()));
    DrawStr(g, left, fSmall, RectF(14 * s, out.h - 24 * s, 200 * s, 20 * s), kDim);
    DrawStr(g, right, fSmall, RectF(150 * s, out.h - 24 * s, out.w - 164 * s, 20 * s), kDim, Gdiplus::StringAlignmentFar);
}
void RenderPanel(const std::wstring& title, const std::vector<PanelLine>& lines, float scale, HudImage& out, float width) {
    const float s = scale;
    out.w = static_cast<int>(width * s + 0.5f);
    out.h = static_cast<int>((46 + 26 * static_cast<float>(lines.size()) + 10) * s + 0.5f);
    out.px.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h), 0u);
    Gdiplus::Bitmap bmp(out.w, out.h, out.w * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(out.px.data()));
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));
    const RectF card(0, 0, static_cast<float>(out.w), static_cast<float>(out.h));
    FillRR(g, card, 12 * s, kSurface, 238);
    StrokeRR(g, card, 12 * s, kBorder);
    const Gdiplus::Font fH2 = MakeFont(14, s, Gdiplus::FontStyleBold), fSmall = MakeFont(12, s, Gdiplus::FontStyleRegular),
                        fMono = Gdiplus::Font(L"Consolas", 13 * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    DrawStr(g, title, fH2, RectF(14 * s, 8 * s, (width - 28) * s, 24 * s), kText);
    Gdiplus::Pen sep(Col(kBorder), 1.0f);
    float y = 40 * s;
    for (const PanelLine& l : lines) {
        DrawStr(g, l.key, fSmall, RectF(14 * s, y, 130 * s, 26 * s), kDim);
        DrawStr(g, l.value, fMono, RectF(140 * s, y, (width - 154) * s, 26 * s), l.color ? l.color : kText, Gdiplus::StringAlignmentFar);
        y += 26 * s;
        g.DrawLine(&sep, 14 * s, y - 0.5f, (width - 14) * s, y - 0.5f);
    }
}

}  // namespace forts
