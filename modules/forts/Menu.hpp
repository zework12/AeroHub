#pragma once
// In-game cheat menu. Painted with the shared GDI+ look (core/Ui.hpp) into a pixel buffer that the
// overlay uploads as a texture, so it matches the hub window. All coordinates are viewport pixels.
// Layout: header (brand, status pill, close; the header is also the drag handle), tab strip, rows of the selected tab, footer hint.
// A row: name + one-line description on the left; on the right an optional key box and mode box (key binds) and the control
// (switch, value button or Run button).
#include <windows.h>

#include <string>
#include <vector>

namespace forts {

struct MenuRow {
    std::wstring name;
    std::wstring desc;     // one grey line under the name
    bool toggle = true;    // false = one-shot action ("Run" button)
    bool on = false;
    bool section = false;  // non-clickable caption row
    std::wstring value;    // non-empty: a choice row; shows this value and cycles on click
    int id = -1;           // owner-defined action id, returned untouched
    // Key bind of the row's action. `bindable` rows show the key box; toggles also show the Toggle/Hold box.
    bool bindable = false;
    std::wstring bindText;  // the key's name, empty = none
    bool bindHold = false;
    bool capturing = false;  // waiting for a key press
};

struct MenuView {
    std::vector<MenuRow> rows;  // rows of the selected tab
    std::vector<std::wstring> tabs;
    int tab = 0;
    std::wstring subtitle;
    std::wstring pill;
    COLORREF pillColor = 0;
    int keyboardFocus = -1;  // row with the keyboard focus ring, -1 for none
    // Fixed texts, replaceable for another language.
    std::wstring on = L"ON", off = L"OFF", run = L"Run", noKey = L"-", pressKey = L"Press a key...", modeToggle = L"Toggle", modeHold = L"Hold";
    std::wstring footer = L"Click a key box and press a key (Esc clears).  Toggle: a press switches it.  Hold: on only while the key is held.";
};

enum class MenuHit { None, Row, Close, Tab, Key, Mode };

class Menu {
public:
    // Sets scale and geometry for the given viewport, rows of the current tab and number of tabs. The window keeps its
    // position between calls (it can be dragged); `userScale` multiplies the size chosen from the viewport height.
    void Layout(int viewportW, int viewportH, const std::vector<MenuRow>& rows, int tabCount, float userScale = 1.0f);
    bool Inside(float x, float y) const;
    bool Pressing() const { return pressed_ != MenuHit::None || dragging_; }

    // Feed the pointer once per frame. Returns true when a click completed on a row / tab / key box / mode box / the close button;
    // `index` is the row or tab number. Dragging the header moves the window and returns false.
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
    int w_ = 0, h_ = 0, rowCount_ = 0, tabCount_ = 0, vw_ = 0, vh_ = 0;
    bool placed_ = false, dragging_ = false;
    float grabX_ = 0, grabY_ = 0;
    MenuHit hover_ = MenuHit::None, pressed_ = MenuHit::None;
    int hoverIndex_ = -1, pressedIndex_ = -1;
    bool wasDown_ = false;
    std::vector<unsigned> pixels_;
    std::vector<unsigned> cursor_;
    int cursorSize_ = 0;
    float cursorScale_ = 0;
    std::vector<unsigned char> rowKind_;  // per row: bits 0-1: 0 caption, 1 plain, 2 key box, 3 key + mode box; bits 2-3: control (1 switch, 2 value, 3 Run)
    std::vector<float> rowY_;             // top of each row in menu units (plus one end entry)
    std::wstring lastKey_;
};

}  // namespace forts
