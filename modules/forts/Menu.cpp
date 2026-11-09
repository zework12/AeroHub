#include "Menu.hpp"

#include <algorithm>
#include <memory>

#include "Ui.hpp"

namespace forts {
namespace {

using namespace aero::ui;

constexpr float kWidth = 820, kHeader = 76, kTabs = 56, kRowH = 58, kSectionH = 36, kFooter = 46, kPad = 10;
constexpr float kSwitchW = 52, kSwitchH = 26, kValueW = 150, kRunW = 96, kKeyW = 118, kModeW = 104, kChipH = 32, kGap = 8;

RectF CloseRect(float s) { return RectF((kWidth - 18 - 32) * s, 22 * s, 32 * s, 32 * s); }
RectF TabRect(float s, int i, int count) {
    const float total = (kWidth - 2 * kPad) * s, each = total / static_cast<float>(std::max(1, count));
    return RectF(kPad * s + each * static_cast<float>(i), (kHeader + 4) * s, each, 44 * s);
}

// What a row shows on its right side; rebuilt from the MenuRow in Layout so hit-testing and painting agree.
struct RowInfo {
    bool section = false, bindable = false, modeBox = false;
    int ctrl = 0;  // 1 switch, 2 value button, 3 Run button
};

struct Geo {
    RectF ctrl, key, mode;
    float textRight = 0;  // right edge of the name/description area
};

Geo MakeGeo(float s, const RectF& rr, const RowInfo& info) {
    Geo g;
    const float right = rr.X + rr.Width - 16 * s;
    float left = right;
    if (info.ctrl == 1) {
        g.ctrl = RectF(right - kSwitchW * s, rr.Y + (rr.Height - kSwitchH * s) / 2, kSwitchW * s, kSwitchH * s);
        left = g.ctrl.X - 52 * s;  // room for the ON/OFF word
    } else if (info.ctrl == 2) {
        g.ctrl = RectF(right - kValueW * s, rr.Y + (rr.Height - 34 * s) / 2, kValueW * s, 34 * s);
        left = g.ctrl.X;
    } else if (info.ctrl == 3) {
        g.ctrl = RectF(right - kRunW * s, rr.Y + (rr.Height - 34 * s) / 2, kRunW * s, 34 * s);
        left = g.ctrl.X;
    }
    const float cy = rr.Y + (rr.Height - kChipH * s) / 2;
    if (info.modeBox) {
        g.mode = RectF(left - kGap * s - kModeW * s, cy, kModeW * s, kChipH * s);
        left = g.mode.X;
    }
    if (info.bindable) {
        g.key = RectF(left - kGap * s - kKeyW * s, cy, kKeyW * s, kChipH * s);
        left = g.key.X;
    }
    g.textRight = left - 14 * s;
    return g;
}

}  // namespace

void Menu::Layout(int vw, int vh, const std::vector<MenuRow>& rows, int tabCount, float userScale) {
    vw_ = vw;
    vh_ = vh;
    scale_ = std::clamp(static_cast<float>(vh) / 1080.0f * std::clamp(userScale, 0.6f, 3.0f), 0.6f, 3.2f);
    rowCount_ = static_cast<int>(rows.size());
    tabCount_ = tabCount;
    rowKind_.assign(rows.size(), 1);
    float rowsH = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const MenuRow& r = rows[i];
        unsigned char kind = 1;
        if (r.section) kind = 0;
        else if (r.bindable) kind = r.toggle && r.value.empty() ? 3 : 2;
        rowKind_[i] = kind;
        rowsH += r.section ? kSectionH : kRowH;
    }
    // Per-row control type is kept in the high bits: 4 switch, 8 value button, 12 Run button.
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const MenuRow& r = rows[i];
        if (r.section) continue;
        const unsigned char ctrl = !r.value.empty() ? 8 : (r.toggle ? 4 : 12);
        rowKind_[i] = static_cast<unsigned char>(rowKind_[i] | ctrl);
    }
    const float height = kHeader + kTabs + rowsH + kFooter + 8;
    w_ = static_cast<int>(kWidth * scale_ + 0.5f);
    h_ = static_cast<int>(height * scale_ + 0.5f);
    if (!placed_) {
        x_ = std::max(0.0f, std::min(40.0f * scale_, static_cast<float>(vw - w_)));
        y_ = std::max(0.0f, std::min(60.0f * scale_, static_cast<float>(vh - h_)));
        placed_ = true;
    }
    x_ = std::clamp(x_, std::min(0.0f, static_cast<float>(vw - w_)), std::max(0.0f, static_cast<float>(vw - w_)));
    y_ = std::clamp(y_, std::min(0.0f, static_cast<float>(vh - h_)), std::max(0.0f, static_cast<float>(vh - h_)));
    rowY_.assign(rows.size() + 1, 0.0f);
    float y = kHeader + kTabs;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rowY_[i] = y;
        y += rows[i].section ? kSectionH : kRowH;
    }
    rowY_[rows.size()] = y;
}

bool Menu::Inside(float x, float y) const { return x >= x_ && y >= y_ && x < x_ + static_cast<float>(w_) && y < y_ + static_cast<float>(h_); }

namespace {
RowInfo InfoOf(unsigned char kind) {
    RowInfo i;
    i.section = (kind & 3) == 0;
    i.bindable = (kind & 3) >= 2;
    i.modeBox = (kind & 3) == 3;
    i.ctrl = (kind >> 2) & 3;
    return i;
}
}  // namespace

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
        const RowInfo info = InfoOf(rowKind_[static_cast<std::size_t>(i)]);
        if (info.section) continue;
        const RectF rr(kPad * scale_, rowY_[static_cast<std::size_t>(i)] * scale_, (kWidth - 2 * kPad) * scale_, (kRowH - 4) * scale_);
        if (!rr.Contains(p)) continue;
        const Geo g = MakeGeo(scale_, rr, info);
        index = i;
        if (info.bindable && g.key.Contains(p)) hit = MenuHit::Key;
        else if (info.modeBox && g.mode.Contains(p)) hit = MenuHit::Mode;
        else hit = MenuHit::Row;
        return true;
    }
    return false;
}

void Menu::ClearPointer() {
    hover_ = pressed_ = MenuHit::None;
    hoverIndex_ = pressedIndex_ = -1;
    wasDown_ = false;
    dragging_ = false;
}

bool Menu::Pointer(float x, float y, bool leftDown, MenuHit& hit, int& index) {
    MenuHit h = MenuHit::None;
    int r = -1;
    if (!Inside(x, y) || !HitTest(x - x_, y - y_, h, r)) {
        h = MenuHit::None;
        r = -1;
    }
    if (dragging_) {  // moving the window: no hover or clicks meanwhile
        h = MenuHit::None;
        r = -1;
    }
    hover_ = h;
    hoverIndex_ = r;
    // A press that starts on the header (but not on the close button) grabs the window.
    if (leftDown && !wasDown_ && Inside(x, y) && h == MenuHit::None && (y - y_) < kHeader * scale_) {
        dragging_ = true;
        grabX_ = x - x_;
        grabY_ = y - y_;
    }
    if (dragging_) {
        if (!leftDown) {
            dragging_ = false;
        } else {
            x_ = std::clamp(x - grabX_, std::min(0.0f, static_cast<float>(vw_ - w_)), std::max(0.0f, static_cast<float>(vw_ - w_)));
            y_ = std::clamp(y - grabY_, std::min(0.0f, static_cast<float>(vh_ - h_)), std::max(0.0f, static_cast<float>(vh_ - h_)));
        }
        wasDown_ = leftDown;
        return false;
    }
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
    key += v.on + L"|" + v.off + L"|" + v.run + L"|" + v.noKey + L"|" + v.pressKey + L"|" + v.modeToggle + L"|" + v.modeHold + L"|" + v.footer + L"|";
    for (const std::wstring& t : v.tabs) key += t + L",";
    for (const MenuRow& r : v.rows)
        key += r.name + L"/" + r.desc + (r.on ? L"1" : L"0") + (r.toggle ? L"t" : L"b") + (r.section ? L"s" : L"") + r.value +
               (r.bindable ? L"k" + r.bindText + (r.bindHold ? L"h" : L"p") + (r.capturing ? L"c" : L"") : L"") + L";";
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
    const Gdiplus::Font fTitle = font(19, Gdiplus::FontStyleBold), fRow = font(16, Gdiplus::FontStyleBold), fBody = font(15, Gdiplus::FontStyleRegular),
                        fBold = font(15, Gdiplus::FontStyleBold), fSmall = font(13, Gdiplus::FontStyleRegular),
                        fCaption = font(12, Gdiplus::FontStyleBold), fBrand = font(20, Gdiplus::FontStyleBold);

    const RectF card(0, 0, static_cast<float>(w_), static_cast<float>(h_));
    FillRR(g, card, 14 * s, kSurface, 246);
    StrokeRR(g, card, 14 * s, kBorder);

    // Header: brand, title, status pill, close button. Dragging the header moves the window.
    FillRR(g, RectF(18 * s, 18 * s, 40 * s, 40 * s), 11 * s, kAccent);
    DrawStr(g, L"A", fBrand, RectF(18 * s, 18 * s, 40 * s, 40 * s), RGB(255, 255, 255), Gdiplus::StringAlignmentCenter);
    DrawStr(g, L"AeroHub", fTitle, RectF(72 * s, 14 * s, 200 * s, 28 * s), kText);
    DrawStr(g, v.subtitle, fSmall, RectF(72 * s, 42 * s, 300 * s, 20 * s), kDim);
    if (!v.pill.empty()) {
        const float pw = 12 * s * 2 + 8 * s + 8 * s + MeasureW(g, v.pill, fSmall);
        DrawPill(g, s, fSmall, (kWidth - 18 - 32 - 12) * s - pw, 24 * s, v.pill, v.pillColor);
    }
    const RectF close = CloseRect(s);
    if (hover_ == MenuHit::Close) FillRR(g, close, 9 * s, kSurface2);
    {
        Gdiplus::Pen pen(Col(pressed_ == MenuHit::Close ? kText : kDim), 2.2f * s);
        const float cx = close.X + close.Width / 2, cy = close.Y + close.Height / 2, d = 6 * s;
        g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
        g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
    }

    // Tab strip (segmented control).
    const int tabCount = static_cast<int>(v.tabs.size());
    if (tabCount > 0) {
        const RectF strip(kPad * s, (kHeader + 2) * s, (kWidth - 2 * kPad) * s, 48 * s);
        FillRR(g, strip, 11 * s, kBg, 230);
        for (int i = 0; i < tabCount; ++i) {
            RectF r = TabRect(s, i, tabCount);
            r = RectF(r.X + 4 * s, r.Y + 2 * s, r.Width - 8 * s, r.Height - 4 * s);
            const bool active = i == v.tab;
            const bool hov = hover_ == MenuHit::Tab && hoverIndex_ == i;
            if (active) FillRR(g, r, 9 * s, kSurface2);
            else if (hov) FillRR(g, r, 9 * s, kSurface2, 110);
            DrawStr(g, v.tabs[static_cast<std::size_t>(i)], active ? fBold : fBody, r, active ? kText : kDim, Gdiplus::StringAlignmentCenter);
            if (active) FillRR(g, RectF(r.X + r.Width / 2 - 16 * s, r.Y + r.Height - 4 * s, 32 * s, 3 * s), 1.5f * s, kAccentRing);
        }
    }
    Gdiplus::Pen sep(Col(kBorder), 1.0f);

    // Rows: a card per function; the whole card is clickable except the key and mode boxes.
    for (int i = 0; i < static_cast<int>(v.rows.size()); ++i) {
        const MenuRow& r = v.rows[static_cast<std::size_t>(i)];
        const RowInfo info = InfoOf(rowKind_[static_cast<std::size_t>(i)]);
        const float top = rowY_[static_cast<std::size_t>(i)];
        if (r.section) {
            DrawStr(g, r.name, fCaption, RectF((kPad + 6) * s, (top + 10) * s, (kWidth - 2 * kPad - 12) * s, (kSectionH - 12) * s), kAccentRing,
                    Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentCenter);
            g.DrawLine(&sep, (kPad + 6) * s, (top + kSectionH - 2) * s, (kWidth - kPad - 6) * s, (top + kSectionH - 2) * s);
            continue;
        }
        const RectF rr(kPad * s, top * s, (kWidth - 2 * kPad) * s, (kRowH - 4) * s);
        const Geo geo = MakeGeo(s, rr, info);
        const bool hovRow = (hover_ == MenuHit::Row || hover_ == MenuHit::Key || hover_ == MenuHit::Mode) && hoverIndex_ == i;
        const bool hovMain = hover_ == MenuHit::Row && hoverIndex_ == i;
        const bool downMain = pressed_ == MenuHit::Row && pressedIndex_ == i && hovMain;
        FillRR(g, rr, 10 * s, downMain ? kBorder : (hovMain ? kSurface2 : kBg), downMain || hovMain ? 255 : 120);
        if (hovRow || r.on) StrokeRR(g, rr, 10 * s, r.on && info.ctrl == 1 ? kAccent : kBorder, 1.0f, r.on && info.ctrl == 1 ? 160 : 255);
        if (v.keyboardFocus == i) StrokeRR(g, rr, 10 * s, kAccentRing, 2.0f);

        const float textW = std::max(40.0f * s, geo.textRight - (rr.X + 16 * s));
        DrawStr(g, r.name, fRow, RectF(rr.X + 16 * s, rr.Y + 6 * s, textW, 24 * s), kText);
        if (!r.desc.empty()) DrawStr(g, r.desc, fSmall, RectF(rr.X + 16 * s, rr.Y + 30 * s, textW, 20 * s), kDim);

        if (info.bindable) {
            const bool hov = hover_ == MenuHit::Key && hoverIndex_ == i;
            const bool down = pressed_ == MenuHit::Key && pressedIndex_ == i && hov;
            const std::wstring label = r.capturing ? v.pressKey : (r.bindText.empty() ? v.noKey : r.bindText);
            DrawButton(g, s, r.bindText.empty() && !r.capturing ? fBody : fBold, geo.key, label, r.capturing, true, hov, down);
            if (r.capturing) StrokeRR(g, RectF(geo.key.X - 2, geo.key.Y - 2, geo.key.Width + 4, geo.key.Height + 4), 10 * s, kAccentRing, 2.0f);
        }
        if (info.modeBox) {
            const bool hov = hover_ == MenuHit::Mode && hoverIndex_ == i;
            const bool down = pressed_ == MenuHit::Mode && pressedIndex_ == i && hov;
            DrawButton(g, s, fBody, geo.mode, r.bindHold ? v.modeHold : v.modeToggle, false, !r.bindText.empty(), hov, down);
        }
        if (info.ctrl == 2) {
            DrawButton(g, s, fBold, geo.ctrl, r.value, false, true, hovMain, downMain);
        } else if (info.ctrl == 1) {
            DrawStr(g, r.on ? v.on : v.off, fSmall, RectF(geo.ctrl.X - 50 * s, rr.Y, 42 * s, rr.Height), r.on ? kOk : kDim, Gdiplus::StringAlignmentFar);
            DrawSwitch(g, s, geo.ctrl, r.on, true, hovMain);
        } else if (info.ctrl == 3) {
            DrawButton(g, s, fBold, geo.ctrl, v.run, false, true, hovMain, downMain);
        }
    }

    const float fy = rowY_[v.rows.size()] * s + 4 * s;
    g.DrawLine(&sep, 16 * s, fy, (kWidth - 16) * s, fy);
    DrawStr(g, v.footer, fSmall, RectF(18 * s, fy + 2 * s, (kWidth - 36) * s, 34 * s), kDim);
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
