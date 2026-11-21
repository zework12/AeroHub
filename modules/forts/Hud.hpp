#pragma once
// Small GDI+ rendered HUD pieces (enemy labels, enemy-base radar), painted in the shared AeroHub look and
// uploaded by the overlay as textures.
#include <windows.h>

#include <string>
#include <vector>

namespace forts {

struct HudImage {
    int w = 0, h = 0;
    std::vector<unsigned> px;  // premultiplied BGRA, row 0 = top
};

// A small pill with a caption, used next to enemy devices.
void RenderLabel(const std::wstring& text, COLORREF accent, float scale, HudImage& out);

// One beam of the base being watched (world coordinates).
struct ViewSegment {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float health = 1;       // 0..1
    bool building = false;  // not finished yet
};

struct ViewDevice {
    float x = 0, y = 0;  // world coordinates
    int kind = 0;        // 0 other device, 1 weapon, 2 reactor
    std::string label;   // weapon type / "reactor"
};

// "Observer" window: the whole base drawn from above (beams as lines coloured by health, devices as
// markers), zoomed out automatically so a bigger base is always fully visible.
// baseIndex / baseCount: which enemy base is shown; with more than one, arrows in the header switch (see BaseViewArrow).
void RenderBaseView(const std::vector<ViewSegment>& segments, const std::vector<ViewDevice>& devices, float scale, HudImage& out,
                    int baseIndex = 0, int baseCount = 1);
// Hit test for those arrows, in the panel's own pixels: -1 left, +1 right, 0 none.
int BaseViewArrow(float x, float y, float scale);

// Compact key / value panel (used for the spy-style enemy intel).
struct PanelLine {
    std::wstring key, value;
    COLORREF color = 0;  // value colour; 0 = default text colour
};
// `width` is in unscaled pixels (the key column stays 130 wide, the value column takes the rest).
void RenderPanel(const std::wstring& title, const std::vector<PanelLine>& lines, float scale, HudImage& out, float width = 300.0f);

}  // namespace forts
