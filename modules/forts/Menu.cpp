#include "Menu.hpp"

#include <algorithm>
#include <memory>

#include "Ui.hpp"

namespace forts {
namespace {

using namespace aero::ui;

constexpr float kWidth = 420, kHeader = 64, kTabs = 48, kRowH = 44, kFooter = 40, kPad = 10;

RectF CloseRect(float s) { return RectF((kWidth - 16 - 28) * s, 18 * s, 28 * s, 28 * s); }
RectF TabRect(float s, int i, int count) {
    const float total = (kWidth - 2 * kPad) * s, each = total / static_cast<float>(std::max(1, count));
    return RectF(kPad * s + each * static_cast<float>(i), (kHeader + 4) * s, each, 36 * s);
}
RectF RowRect(float s, int i) { return RectF(kPad * s, (kHeader + kTabs + kRowH * static_cast<float>(i)) * s, (kWidth - 2 * kPad) * s, (kRowH - 4) * s); }

}  // namespace

void Menu::Layout(int vw, int vh, const std::vector<MenuRow>& rows, int tabCount) {
    scale_ = std::clamp(static_cast<float>(vh) / 1080.0f, 0.8f, 2.5f);
    rowCount_ = static_cast<int>(rows.size());
    tabCount_ = tabCount;
    section_.assign(rows.size(), false);
    for (std::size_t i = 0; i < rows.size(); ++i) section_[i] = rows[i].section;
    const float height = kHeader + kTabs + kRowH * static_cast<float>(rowCount_) + kFooter + 6;
    w_ = static_cast<int>(kWidth * scale_ + 0.5f);
    h_ = static_cast<int>(height * scale_ + 0.5f);
    x_ = std::min(40.0f * scale_, std::max(0.0f, static_cast<float>(vw - w_)));
    y_ = std::min(100.0f * scale_, std::max(0.0f, static_cast<float>(vh - h_)));
}

bool Menu::Inside(float x, float y) const { return x >= x_ && y >= y_ && x < x_ + static_cast<float>(w_) && y < y_ + static_cast<float>(h_); }

bool Menu::HitTest(float lx, float ly, MenuHit& hit, int& index) const {
    const Gdiplus::PointF p(lx, ly);
    if (CloseRect(scale_).Contains(p)) {
        hit = MenuHit::Close;
        index = -1;
        return true;
    }
    for (int i = 0; i < tabCount_; ++i) {
        if (TabRect(scale_, i, tabCount_).Contains(p)) {
            hit = MenuHit::Tab;
            index = i;
            return true;
        }
    }
    for (int i = 0; i < rowCount_; ++i) {
        if (section_[static_cast<std::size_t>(i)]) continue;
        if (RowRect(scale_, i).Contains(p)) {
            hit = MenuHit::Row;
            index = i;
            return true;
        }
    }
    return false;
}

void Menu::ClearPointer() {
    hover_ = pressed_ = MenuHit::None;
    hoverIndex_ = pressedIndex_ = -1;
    wasDown_ = false;
}

bool Menu::Pointer(float x, float y, bool leftDown, MenuHit& hit, int& index) {
    MenuHit h = MenuHit::None;
    int r = -1;
    if (!Inside(x, y) || !HitTest(x - x_, y - y_, h, r)) {
        h = MenuHit::None;
        r = -1;
    }
    hover_ = h;
    hoverIndex_ = r;
    bool activated = false;
    if (leftDown && !wasDown_ && Inside(x, y)) {  // press started on the menu (also on its empty parts)
        pressed_ = h;
        pressedIndex_ = r;
    }
    if (!leftDown && wasDown_) {
        if (pressed_ != MenuHit::None && pressed_ == h && pressedIndex_ == r) {
            activated = true;
            hit = h;
            index = r;
        }
        pressed_ = MenuHit::None;
        pressedIndex_ = -1;
    }
    wasDown_ = leftDown;
    return activated;
}

bool Menu::Render(const MenuView& v) {
    // Cheap change detection: everything that affects the picture goes into one key string.
    std::wstring key = std::to_wstring(w_) + L"|" + std::to_wstring(h_) + L"|" + v.pill + L"|" + v.subtitle + L"|" +
                       std::to_wstring(static_cast<int>(hover_)) + L"," + std::to_wstring(hoverIndex_) + L"|" +
                       std::to_wstring(static_cast<int>(pressed_)) + L"," + std::to_wstring(pressedIndex_) + L"|" +
                       std::to_wstring(v.keyboardFocus) + L"|" + std::to_wstring(v.tab) + L"|";
    for (const std::wstring& t : v.tabs) key += t + L",";
    for (const MenuRow& r : v.rows) key += r.name + (r.on ? L"1" : L"0") + (r.toggle ? L"t" : L"b") + (r.section ? L"s" : L"") + r.value + L";";
    if (key == lastKey_ && !pixels_.empty()) return false;
    lastKey_ = key;

    pixels_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), 0u);
    Gdiplus::Bitmap bmp(w_, h_, w_ * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(pixels_.data()));
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);  // ClearType needs an opaque background
    g.Clear(Color(0, 0, 0, 0));

    const float s = scale_;
    auto font = [&](float px, int style) { return Gdiplus::Font(L"Segoe UI", px * s, style, Gdiplus::UnitPixel); };
    const Gdiplus::Font fH2 = font(15, Gdiplus::FontStyleBold), fBody = font(14, Gdiplus::FontStyleRegular),
                        fBold = font(14, Gdiplus::FontStyleBold), fSmall = font(12, Gdiplus::FontStyleRegular),
                        fBrand = font(17, Gdiplus::FontStyleBold);

    const RectF card(0, 0, static_cast<float>(w_), static_cast<float>(h_));
    FillRR(g, card, 12 * s, kSurface, 244);
    StrokeRR(g, card, 12 * s, kBorder);

    // Header: brand, title, status pill, close button.
    FillRR(g, RectF(16 * s, 16 * s, 32 * s, 32 * s), 9 * s, kAccent);
    DrawStr(g, L"A", fBrand, RectF(16 * s, 16 * s, 32 * s, 32 * s), RGB(255, 255, 255), Gdiplus::StringAlignmentCenter);
    DrawStr(g, L"AeroHub", fH2, RectF(58 * s, 14 * s, 140 * s, 22 * s), kText);
    DrawStr(g, v.subtitle, fSmall, RectF(58 * s, 34 * s, 140 * s, 18 * s), kDim);
    if (!v.pill.empty()) {
        const float pw = 12 * s * 2 + 8 * s + 8 * s + MeasureW(g, v.pill, fSmall);
        DrawPill(g, s, fSmall, (kWidth - 16 - 28 - 10) * s - pw, 19 * s, v.pill, v.pillColor);
    }
    const RectF close = CloseRect(s);
    if (hover_ == MenuHit::Close) FillRR(g, close, 8 * s, kSurface2);
    {
        Gdiplus::Pen pen(Col(pressed_ == MenuHit::Close ? kText : kDim), 2.0f * s);
        const float cx = close.X + close.Width / 2, cy = close.Y + close.Height / 2, d = 5 * s;
        g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
        g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
    }

    // Tab strip (segmented control).
    const int tabCount = static_cast<int>(v.tabs.size());
    if (tabCount > 0) {
        const RectF strip(kPad * s, (kHeader + 2) * s, (kWidth - 2 * kPad) * s, 40 * s);
        FillRR(g, strip, 9 * s, kBg, 230);
        for (int i = 0; i < tabCount; ++i) {
            RectF r = TabRect(s, i, tabCount);
            r = RectF(r.X + 3 * s, r.Y + 1 * s, r.Width - 6 * s, r.Height - 2 * s);
            const bool active = i == v.tab;
            const bool hov = hover_ == MenuHit::Tab && hoverIndex_ == i;
            if (active) FillRR(g, r, 7 * s, kSurface2);
            else if (hov) FillRR(g, r, 7 * s, kSurface2, 110);
            DrawStr(g, v.tabs[static_cast<std::size_t>(i)], active ? fBold : fSmall, r, active ? kText : kDim, Gdiplus::StringAlignmentCenter);
            if (active) FillRR(g, RectF(r.X + r.Width / 2 - 14 * s, r.Y + r.Height - 3 * s, 28 * s, 2.5f * s), 1.2f * s, kAccentRing);
        }
    }
    Gdiplus::Pen sep(Col(kBorder), 1.0f);

    // Rows: whole row is clickable; switches for toggles, "Run" button for actions.
    for (int i = 0; i < static_cast<int>(v.rows.size()); ++i) {
        const MenuRow& r = v.rows[static_cast<std::size_t>(i)];
        const RectF rr = RowRect(s, i);
        if (r.section) {
            DrawStr(g, r.name, fSmall, RectF(rr.X + 14 * s, rr.Y + 8 * s, rr.Width - 28 * s, rr.Height - 8 * s), kDim,
                    Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentCenter);
            continue;
        }
        const bool hov = hover_ == MenuHit::Row && hoverIndex_ == i;
        const bool down = pressed_ == MenuHit::Row && pressedIndex_ == i && hov;
        if (hov) FillRR(g, rr, 8 * s, down ? kBorder : kSurface2);
        if (v.keyboardFocus == i) StrokeRR(g, rr, 8 * s, kAccentRing, 2.0f);
        DrawStr(g, r.name, fBody, RectF(rr.X + 14 * s, rr.Y, rr.Width - 130 * s, rr.Height), kText);
        if (!r.value.empty()) {
            const float vw = 116 * s;
            DrawButton(g, s, fBold, RectF(rr.X + rr.Width - 14 * s - vw, rr.Y + (rr.Height - 30 * s) / 2, vw, 30 * s), r.value, false, true, hov,
                       down);
        } else if (r.toggle) {
            const RectF sw(rr.X + rr.Width - 14 * s - 44 * s, rr.Y + (rr.Height - 24 * s) / 2, 44 * s, 24 * s);
            DrawStr(g, r.on ? L"ON" : L"OFF", fSmall, RectF(sw.X - 46 * s, rr.Y, 38 * s, rr.Height), r.on ? kOk : kDim,
                    Gdiplus::StringAlignmentFar);
            DrawSwitch(g, s, sw, r.on, true, hov);
        } else {
            DrawButton(g, s, fBold, RectF(rr.X + rr.Width - 14 * s - 66 * s, rr.Y + (rr.Height - 30 * s) / 2, 66 * s, 30 * s), L"Run",
                       false, true, hov, down);
        }
    }

    const float fy = (kHeader + kTabs + kRowH * static_cast<float>(v.rows.size())) * s + 4 * s;
    g.DrawLine(&sep, 16 * s, fy, (kWidth - 16) * s, fy);
    DrawStr(g, L"Click a row or tab.  Arrow keys also work.  Insert closes.", fSmall, RectF(16 * s, fy + 2 * s, (kWidth - 32) * s, 32 * s), kDim);
    return true;
}

const void* Menu::CursorPixels(int& w, int& h) {
    if (cursor_.empty() || cursorScale_ != scale_) {
        cursorScale_ = scale_;
        cursorSize_ = static_cast<int>(28.0f * scale_ + 0.5f);
        cursor_.assign(static_cast<std::size_t>(cursorSize_) * static_cast<std::size_t>(cursorSize_), 0u);
        Gdiplus::Bitmap bmp(cursorSize_, cursorSize_, cursorSize_ * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(cursor_.data()));
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Color(0, 0, 0, 0));
        const float s = scale_;
        const Gdiplus::PointF pts[7] = {{2 * s, 2 * s}, {2 * s, 21 * s}, {7 * s, 16.5f * s}, {10.5f * s, 24 * s},
                                        {14 * s, 22.5f * s}, {10.5f * s, 15 * s}, {17.5f * s, 15 * s}};
        Gdiplus::SolidBrush fill(Col(RGB(255, 255, 255)));
        Gdiplus::Pen outline(Col(RGB(14, 17, 22)), 1.6f * s);
        outline.SetLineJoin(Gdiplus::LineJoinRound);
        g.FillPolygon(&fill, pts, 7);
        g.DrawPolygon(&outline, pts, 7);
    }
    w = h = cursorSize_;
    return cursor_.data();
}

}  // namespace forts
