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

    // Numerically stable form of the closed solution (double precision). With A(t) = (1 - e^(-k t)) / k and
    // B(t) = (t - A) / k:  x = x0 + vx0 A,  y = y0 + vy0 A + g B,  vy = vy0 e^(-k t) + g A.
    // For small k t the quotients lose all digits (k = 0.0001 made the old float form jump by thousands of units),
    // so a Taylor series is used there; k = 0 falls out of it exactly (A = t, B = t^2 / 2).
    const float gDir = gravityY >= 0.0f ? 1.0f : -1.0f;
    const double g = gravityY, k = drag;
    int n = 0;
    for (; n < cap; ++n) {
        const double t = static_cast<double>(n) * static_cast<double>(dt);
        if (t > static_cast<double>(maxTime)) break;
        const double x = k * t;
        double a, b;
        if (x < 1.0e-3) {
            a = t * (1.0 - x / 2.0 + x * x / 6.0 - x * x * x / 24.0 + x * x * x * x / 120.0);
            b = t * t * (0.5 - x / 6.0 + x * x / 24.0 - x * x * x / 120.0);
        } else {
            a = -std::expm1(-x) / k;
            b = (t - a) / k;
        }
        const Vec2f p{static_cast<float>(p0.x + v0.x * a), static_cast<float>(p0.y + v0.y * a + g * b)};
        const double vy = v0.y * std::exp(-x) + g * a;
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) break;
        out[n] = p;
        const bool pastApex = vy * gDir > 0.0;
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
