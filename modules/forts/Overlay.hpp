#pragma once
#include <windows.h>

namespace forts {

// Immediate-mode OpenGL drawing inside the game's SwapBuffers call (compatibility contexts only).
class Overlay {
public:
    // Saves GL state and sets a pixel-space ortho projection (origin top-left, y down).
    // False if drawing is not possible (no context, core profile, empty viewport).
    bool Begin(HDC dc, int& viewportWidth, int& viewportHeight);
    void Line(const float* xy, int pointCount, float r, float g, float b, float a, float width);
    void Rect(float x, float y, float w, float h, float r, float g, float b, float a);
    // Draws a premultiplied-alpha BGRA image (row 0 = top) at (x, y). Every `key` owns one GL texture; it
    // is (re)uploaded only when `changed` is true, the size changed, or the game recreated its GL context.
    void Image(const char* key, const void* bgra, int w, int h, bool changed, float x, float y);
    void End();
};

}  // namespace forts
