#pragma once
// Shared look of AeroHub: design tokens and GDI+ drawing primitives. Used by the hub window and by the
// in-game menu of every module, so both are painted by the same code and look identical.
#include <windows.h>

#include <algorithm>
using std::max;  // GDI+ headers expect min/max in the global scope (NOMINMAX is defined project-wide)
using std::min;

#include <objidl.h>

#include <gdiplus.h>

#include <string>

namespace aero::ui {

using Gdiplus::Color;
using Gdiplus::RectF;

// Dark (OLED) surfaces, calm blue accent, semantic status colors that are always paired with text.
inline constexpr COLORREF kBg = RGB(14, 17, 22), kSidebar = RGB(18, 21, 28), kSurface = RGB(22, 26, 34),
                          kSurface2 = RGB(31, 36, 47), kBorder = RGB(43, 49, 62), kText = RGB(232, 235, 242),
                          kDim = RGB(152, 162, 179), kAccent = RGB(37, 99, 235), kAccentHover = RGB(59, 120, 245),
                          kAccentRing = RGB(96, 165, 250), kOk = RGB(52, 211, 153), kWarn = RGB(251, 191, 36),
                          kBad = RGB(248, 113, 113), kTrack = RGB(64, 72, 90);

inline Color Col(COLORREF c, BYTE a = 255) { return Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }

inline void RoundPath(Gdiplus::GraphicsPath& p, const RectF& r, float rad) {
    const float d = min(rad * 2, min(r.Width, r.Height));
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

inline void FillRR(Gdiplus::Graphics& g, const RectF& r, float rad, COLORREF c, BYTE a = 255) {
    Gdiplus::GraphicsPath p;
    RoundPath(p, r, rad);
    Gdiplus::SolidBrush b(Col(c, a));
    g.FillPath(&b, &p);
}

inline void StrokeRR(Gdiplus::Graphics& g, const RectF& r, float rad, COLORREF c, float width = 1.0f, BYTE a = 255) {
    Gdiplus::GraphicsPath p;
    RoundPath(p, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), rad);
    Gdiplus::Pen pen(Col(c, a), width);
    g.DrawPath(&pen, &p);
}

inline void FillDot(Gdiplus::Graphics& g, float cx, float cy, float rad, COLORREF c, BYTE a = 255) {
    Gdiplus::SolidBrush b(Col(c, a));
    g.FillEllipse(&b, cx - rad, cy - rad, rad * 2, rad * 2);
}

inline void DrawStr(Gdiplus::Graphics& g, const std::wstring& s, const Gdiplus::Font& f, const RectF& r, COLORREF c,
                    Gdiplus::StringAlignment h = Gdiplus::StringAlignmentNear,
                    Gdiplus::StringAlignment v = Gdiplus::StringAlignmentCenter) {
    Gdiplus::StringFormat fmt(Gdiplus::StringFormatFlagsNoWrap);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    Gdiplus::SolidBrush b(Col(c));
    g.DrawString(s.c_str(), -1, &f, r, &fmt, &b);
}

inline float MeasureW(Gdiplus::Graphics& g, const std::wstring& s, const Gdiplus::Font& f) {
    Gdiplus::StringFormat fmt(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    RectF box;
    g.MeasureString(s.c_str(), -1, &f, Gdiplus::PointF(0, 0), &fmt, &box);
    return box.Width;
}

// Status pill: dot + word (never color alone). Returns its width.
inline float DrawPill(Gdiplus::Graphics& g, float scale, const Gdiplus::Font& font, float x, float y, const std::wstring& text,
                      COLORREF c) {
    const float h = 26 * scale, padX = 12 * scale, dot = 4 * scale;
    const float w = padX * 2 + dot * 2 + 8 * scale + MeasureW(g, text, font);
    FillRR(g, RectF(x, y, w, h), h / 2, c, 36);
    FillDot(g, x + padX + dot, y + h / 2, dot, c);
    DrawStr(g, text, font, RectF(x + padX + dot * 2 + 8 * scale, y, w, h), c);
    return w;
}

inline void DrawSwitch(Gdiplus::Graphics& g, float scale, const RectF& r, bool on, bool enabled, bool hover) {
    const BYTE a = enabled ? 255 : 110;
    FillRR(g, r, r.Height / 2, on ? (hover ? kAccentHover : kAccent) : (hover ? RGB(78, 87, 108) : kTrack), a);
    const float d = r.Height - 8 * scale;
    const float cx = on ? r.X + r.Width - 4 * scale - d / 2 : r.X + 4 * scale + d / 2;
    FillDot(g, cx, r.Y + r.Height / 2, d / 2, RGB(255, 255, 255), a);
}

inline void DrawButton(Gdiplus::Graphics& g, float scale, const Gdiplus::Font& font, const RectF& r, const std::wstring& label,
                       bool primary, bool enabled, bool hover, bool pressed, bool focus = false) {
    const float rad = 8 * scale;
    if (!enabled) {
        FillRR(g, r, rad, kSurface2, 160);
        StrokeRR(g, r, rad, kBorder);
        DrawStr(g, label, font, r, RGB(112, 121, 138), Gdiplus::StringAlignmentCenter);
        return;
    }
    const COLORREF fill = primary ? (pressed ? RGB(29, 78, 190) : (hover ? kAccentHover : kAccent))
                                  : (pressed ? kBorder : (hover ? RGB(40, 46, 60) : kSurface2));
    FillRR(g, r, rad, fill);
    if (!primary) StrokeRR(g, r, rad, kBorder);
    if (focus) StrokeRR(g, RectF(r.X - 2, r.Y - 2, r.Width + 4, r.Height + 4), rad + 2, kAccentRing, 2.0f);
    DrawStr(g, label, font, r, primary ? RGB(255, 255, 255) : kText, Gdiplus::StringAlignmentCenter);
}

}  // namespace aero::ui
