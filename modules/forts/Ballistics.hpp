#pragma once
// Pure projectile math, no game or Windows dependencies.
// World space is 2D (x, y). gravityY is the signed acceleration along +Y: positive means Y grows
// downward; 0 gives a straight line (beam weapons). Motion model fitted to recorded Forts shots: dv/dt = (0, g) - k * v (linear air drag, k per
// second; k = 0 gives the ideal parabola). Closed form:
//   v(t) = vt + (v0 - vt) * e^(-k t)            with terminal velocity vt = (0, g / k)
//   p(t) = p0 + vt * t + (v0 - vt) * (1 - e^(-k t)) / k
#include <cmath>

namespace forts {

struct Vec2f {
    float x{}, y{};
};

// Writes at most `cap` points spaced `dt` seconds apart, starting at t = 0. Stops after `maxTime`,
// or once the projectile has passed its apex and fallen `dropLimit` units below the start.
// Returns the number of points written (0 for non-finite or degenerate input).
inline int PredictTrajectory(Vec2f p0, Vec2f v0, float gravityY, float drag, float dt, float maxTime, float dropLimit, Vec2f* out,
                             int cap) {
    if (!out || cap <= 0 || !(dt > 0.0f) || !(maxTime > 0.0f) || !(dropLimit > 0.0f)) return 0;
    if (!std::isfinite(p0.x) || !std::isfinite(p0.y) || !std::isfinite(v0.x) || !std::isfinite(v0.y) ||
        !std::isfinite(gravityY) || !std::isfinite(drag) || drag < 0.0f)
        return 0;

    const float gDir = gravityY >= 0.0f ? 1.0f : -1.0f;
    const bool noDrag = drag < 1.0e-5f;
    const float vt = noDrag ? 0.0f : gravityY / drag;  // terminal vertical velocity
    int n = 0;
    for (; n < cap; ++n) {
        const float t = static_cast<float>(n) * dt;
        if (t > maxTime) break;
        Vec2f p;
        float vy;
        if (noDrag) {
            p = {p0.x + v0.x * t, p0.y + v0.y * t + 0.5f * gravityY * t * t};
            vy = v0.y + gravityY * t;
        } else {
            const float e = std::exp(-drag * t);
            const float k = (1.0f - e) / drag;
            p = {p0.x + v0.x * k, p0.y + vt * t + (v0.y - vt) * k};
            vy = vt + (v0.y - vt) * e;
        }
        out[n] = p;
        const bool pastApex = vy * gDir > 0.0f;
        if (pastApex && (p.y - p0.y) * gDir > dropLimit) {
            ++n;
            break;
        }
    }
    return n;
}

// Smallest distance from `point` to the polyline pts[0..n-1] (n >= 1).
inline float DistanceToPolyline(Vec2f point, const Vec2f* pts, int n) {
    if (!pts || n <= 0) return INFINITY;
    float best = INFINITY;
    for (int i = 0; i < n; ++i) {
        const Vec2f a = pts[i];
        const Vec2f b = i + 1 < n ? pts[i + 1] : pts[i];
        const float abx = b.x - a.x, aby = b.y - a.y;
        const float len2 = abx * abx + aby * aby;
        float u = len2 > 0.0f ? ((point.x - a.x) * abx + (point.y - a.y) * aby) / len2 : 0.0f;
        u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
        const float dx = point.x - (a.x + abx * u), dy = point.y - (a.y + aby * u);
        best = std::fmin(best, std::sqrt(dx * dx + dy * dy));
    }
    return best;
}

}  // namespace forts
