#pragma once
// In-game cheat menu. Painted with the shared GDI+ look (core/Ui.hpp) into a pixel buffer that the
// overlay uploads as a texture, so it matches the hub window. All coordinates are viewport pixels.
// Layout: header (brand, status pill, close), tab strip, rows of the selected tab, footer hint.
#include <windows.h>

#include <string>
#include <vector>

namespace forts {

struct MenuRow {
    std::wstring name;
    bool toggle = true;    // false = one-shot action ("Run" button)
    bool on = false;
    bool section = false;  // non-clickable caption row
    std::wstring value;    // non-empty: a choice row; shows this value and cycles on click
    int id = -1;           // owner-defined action id, returned untouched
};

struct MenuView {
    std::vector<MenuRow> rows;  // rows of the selected tab
    std::vector<std::wstring> tabs;
    int tab = 0;
    std::wstring subtitle;
    std::wstring pill;
    COLORREF pillColor = 0;
    int keyboardFocus = -1;  // row with the keyboard focus ring, -1 for none
};

enum class MenuHit { None, Row, Close, Tab };

class Menu {
public:
    // Sets scale and geometry for the given viewport, rows of the current tab and number of tabs.
    void Layout(int viewportW, int viewportH, const std::vector<MenuRow>& rows, int tabCount);
    bool Inside(float x, float y) const;
    bool Pressing() const { return pressed_ != MenuHit::None; }

    // Feed the pointer once per frame. Returns true when a click completed on a row / tab / the close button;
    // `index` is the row or tab number.
    bool Pointer(float x, float y, bool leftDown, MenuHit& hit, int& index);
    void ClearPointer();

    // Repaints only when something visible changed. Returns true if the pixels are new.
    bool Render(const MenuView& view);
    const void* Pixels() const { return pixels_.data(); }
    int Width() const { return w_; }
    int Height() const { return h_; }
    float X() const { return x_; }
    float Y() const { return y_; }
    float Scale() const { return scale_; }
    // Mouse cursor sprite (the game hides its own cursor over the menu); premultiplied BGRA.
    const void* CursorPixels(int& w, int& h);

private:
    bool HitTest(float lx, float ly, MenuHit& hit, int& index) const;

    float scale_ = 1.0f, x_ = 0, y_ = 0;
    int w_ = 0, h_ = 0, rowCount_ = 0, tabCount_ = 0;
    MenuHit hover_ = MenuHit::None, pressed_ = MenuHit::None;
    int hoverIndex_ = -1, pressedIndex_ = -1;
    bool wasDown_ = false;
    std::vector<unsigned> pixels_;
    std::vector<unsigned> cursor_;
    int cursorSize_ = 0;
    float cursorScale_ = 0;
    std::vector<bool> section_;
    std::wstring lastKey_;
};

}  // namespace forts
