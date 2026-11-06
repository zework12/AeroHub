#include "Overlay.hpp"

#include <GL/gl.h>

#include <string>
#include <unordered_map>

#include "Log.hpp"

#ifndef GL_CONTEXT_PROFILE_MASK
#define GL_CONTEXT_PROFILE_MASK 0x9126
#define GL_CONTEXT_CORE_PROFILE_BIT 0x1
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace forts {
namespace {

using PfnUseProgram = void(__stdcall*)(unsigned);
using PfnActiveTexture = void(__stdcall*)(unsigned);
PfnUseProgram g_useProgram = nullptr;
PfnActiveTexture g_activeTexture = nullptr;
GLint g_prevProgram = 0;
bool g_active = false;
bool g_warned = false;

// The menu texture belongs to one GL context; recreated if the game recreates its context.
struct Texture {
    HGLRC context = nullptr;
    GLuint id = 0;
    int w = 0, h = 0;
};
std::unordered_map<std::string, Texture> g_textures;

void WarnOnce(const char* why) {
    if (!g_warned) {
        g_warned = true;
        Logf("overlay unavailable: %s", why);
    }
}

}  // namespace

bool Overlay::Begin(HDC /*dc*/, int& w, int& h) {
    HGLRC context = wglGetCurrentContext();
    if (!context) {
        WarnOnce("no current GL context in SwapBuffers");
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {}
    GLint profile = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if (glGetError() == GL_NO_ERROR && (profile & GL_CONTEXT_CORE_PROFILE_BIT)) {
        WarnOnce("core-profile context (immediate mode unavailable)");
        return false;
    }
    if (!g_useProgram) g_useProgram = reinterpret_cast<PfnUseProgram>(wglGetProcAddress("glUseProgram"));
    if (!g_activeTexture) g_activeTexture = reinterpret_cast<PfnActiveTexture>(wglGetProcAddress("glActiveTexture"));

    GLint vp[4]{};
    glGetIntegerv(GL_VIEWPORT, vp);
    w = vp[2];
    h = vp[3];
    if (w <= 0 || h <= 0) return false;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    g_prevProgram = 0;
    if (g_useProgram) {
        glGetIntegerv(GL_CURRENT_PROGRAM, &g_prevProgram);
        g_useProgram(0);
    }
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_active = true;
    return true;
}

void Overlay::Line(const float* xy, int n, float r, float g, float b, float a, float width) {
    if (!g_active || n < 2) return;
    glLineWidth(width);
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < n; ++i) glVertex2f(xy[2 * i], xy[2 * i + 1]);
    glEnd();
}

void Overlay::Rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!g_active) return;
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

void Overlay::Image(const char* key, const void* bgra, int w, int h, bool changed, float x, float y) {
    if (!g_active || !key || !bgra || w <= 0 || h <= 0) return;
    Texture& tex = g_textures[key];
    if (g_activeTexture) g_activeTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);

    HGLRC context = wglGetCurrentContext();
    bool upload = changed;
    if (!tex.id || tex.context != context) {
        glGenTextures(1, &tex.id);
        tex.context = context;
        upload = true;
    }
    if (tex.w != w || tex.h != h) upload = true;
    glBindTexture(GL_TEXTURE_2D, tex.id);
    if (upload) {
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgra);
        glPopClientAttrib();
        tex.w = w;
        tex.h = h;
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // premultiplied alpha
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(1, 0); glVertex2f(x + static_cast<float>(w), y);
    glTexCoord2f(1, 1); glVertex2f(x + static_cast<float>(w), y + static_cast<float>(h));
    glTexCoord2f(0, 1); glVertex2f(x, y + static_cast<float>(h));
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void Overlay::End() {
    if (!g_active) return;
    g_active = false;
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    if (g_useProgram) g_useProgram(static_cast<unsigned>(g_prevProgram));
    glPopAttrib();
}

}  // namespace forts
