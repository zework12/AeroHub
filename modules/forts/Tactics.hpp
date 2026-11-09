#pragma once
// Pure tactical math for the Forts module (no game or Windows dependencies, so it can be checked on its own).
// World space is the game's: 2D (x, y), y grows DOWNWARD (Physics.Gravity is +981 along +y, GetWaterDepthAt = y - waterLevel).
//
// The projectile physics is the game's own, measured on 479 recorded shots (see tools/ and HANDOFF.md): the simulation runs in fixed
// frames of 0.04 s (Physics.FramesRate = 25). Each frame the velocity is updated first, from gravity and a LINEAR drag taken at the old
// velocity (v += (g - k v) * h), and the position then moves with the NEW velocity (p += v * h). That reproduces the recorded
// positions to the rounding of the log (0.06 units); a continuous closed-form arc is off by g*h*t/2, about 59 units after 3 s.
//   - StepFrame / AdvanceFrames: the physics frame
//   - TraceShot:   a shot's path until it meets terrain or water, with landing point and flight time
//   - SolveAim:    elevation angles that make a shot pass through a target point
//   - EstimateDragFrames: the exact drag of a shell from two of its observed velocities
//   - FindWeakLinks: links whose loss cuts a part of a structure off from the ground (bridges of the support graph)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "Ballistics.hpp"

namespace forts {

inline constexpr double kSimFrame = 0.04;  // seconds per physics frame

struct ShotState {
    double x = 0, y = 0, vx = 0, vy = 0;
};

// One physics frame of a projectile. `g` is the signed gravity along +y, `k` the linear drag per second.
inline void StepFrame(ShotState& s, double g, double k) {
    s.vx -= k * s.vx * kSimFrame;
    s.vy += (g - k * s.vy) * kSimFrame;
    s.x += s.vx * kSimFrame;
    s.y += s.vy * kSimFrame;
}

inline ShotState AdvanceFrames(ShotState s, double g, double k, int frames) {
    for (int i = 0; i < frames; ++i) StepFrame(s, g, k);
    return s;
}

enum class ImpactKind { None, Ground, Water };

struct TraceResult {
    int n = 0;                         // points written to `out` (one per frame), the last one is the landing point when kind != None
    ImpactKind kind = ImpactKind::None;
    Vec2f point{};                     // landing point (or the last point when nothing was hit)
    float time = 0;                    // seconds from the start until the landing point (or until the last point)
};

namespace detail {
inline double Dist(Vec2f a, Vec2f b) {
    const double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
    return std::sqrt(dx * dx + dy * dy);
}
}  // namespace detail

// Follows the shot frame by frame and stops at the first contact with a surface.
//   ground(a, b, hit) -> true when the segment a->b touches terrain; `hit` is the first contact (terrain, not water).
//   water(x)          -> world y of the water surface at x (a huge number on maps without water). "Under water" is y >= level.
// Terrain is asked once per chunk of `chordFrames` frames along the chord of the chunk (cheap); only a chunk that the chord reports
// as touching is then checked frame by frame. The chord leaves the true path by at most g*h^2*chord^2/8 (1.8 units for 3 frames),
// so a terrain feature thinner than that could be missed; chordFrames = 1 asks every frame.
// Water is a separate surface: a shell that is above it at one frame and under it at the next lands on the surface (linear
// interpolation between the two frames). A shot that starts under water ignores the surface until it leaves it.
// Terrain contacts closer than `graceDistance` to the start are ignored (the barrel sits on or in a foundation).
template <class GroundFn, class WaterFn>
TraceResult TraceShot(Vec2f p0, Vec2f v0, float gravityY, float drag, float maxTime, float graceDistance, int chordFrames, GroundFn&& ground,
                      WaterFn&& water, Vec2f* out, int cap) {
    TraceResult r;
    if (!out || cap < 2 || !(maxTime > 0.0f)) return r;
    if (!std::isfinite(p0.x) || !std::isfinite(p0.y) || !std::isfinite(v0.x) || !std::isfinite(v0.y) || !std::isfinite(gravityY) ||
        !std::isfinite(drag) || drag < 0.0f)
        return r;
    constexpr int kMaxChord = 16;
    chordFrames = std::clamp(chordFrames, 1, kMaxChord);
    const int maxFrames = static_cast<int>(std::floor(static_cast<double>(maxTime) / kSimFrame + 1.0e-6));
    ShotState s{p0.x, p0.y, v0.x, v0.y};
    out[0] = p0;
    r.n = 1;
    r.point = p0;
    Vec2f prev = p0;  // the previous frame's point
    bool prevInWater = static_cast<double>(prev.y) >= static_cast<double>(water(prev.x));
    int frame = 0;
    Vec2f chunk[kMaxChord];
    while (frame < maxFrames && r.n < cap) {
        const int m = std::min({chordFrames, maxFrames - frame, cap - r.n});
        ShotState t = s;
        for (int j = 0; j < m; ++j) {
            StepFrame(t, gravityY, drag);
            if (!std::isfinite(t.x) || !std::isfinite(t.y)) return r;
            chunk[j] = {static_cast<float>(t.x), static_cast<float>(t.y)};
        }
        Vec2f hit{};
        const bool refine = m == 1 || ground(prev, chunk[m - 1], hit);
        for (int j = 0; j < m; ++j) {
            const Vec2f cur = chunk[j];
            if (refine && ground(prev, cur, hit) && detail::Dist(hit, p0) >= graceDistance) {
                const double len = detail::Dist(prev, cur);
                const double frac = len > 0.0 ? std::clamp(detail::Dist(prev, hit) / len, 0.0, 1.0) : 1.0;
                out[r.n++] = hit;
                r.kind = ImpactKind::Ground;
                r.point = hit;
                r.time = static_cast<float>((frame + j + frac) * kSimFrame);
                return r;
            }
            const double levelNow = static_cast<double>(water(cur.x));
            const bool inWater = static_cast<double>(cur.y) >= levelNow;
            if (inWater && !prevInWater) {
                const double levelPrev = static_cast<double>(water(prev.x));
                const double fa = static_cast<double>(prev.y) - levelPrev, fb = static_cast<double>(cur.y) - levelNow;  // fa < 0 <= fb
                const double u = fb != fa ? std::clamp(-fa / (fb - fa), 0.0, 1.0) : 1.0;
                const Vec2f w{static_cast<float>(prev.x + (cur.x - prev.x) * u), static_cast<float>(prev.y + (cur.y - prev.y) * u)};
                out[r.n++] = w;
                r.kind = ImpactKind::Water;
                r.point = w;
                r.time = static_cast<float>((frame + j + u) * kSimFrame);
                return r;
            }
            prevInWater = inWater;
            out[r.n++] = cur;
            r.point = cur;
            r.time = static_cast<float>((frame + j + 1) * kSimFrame);
            prev = cur;
        }
        s = t;
        frame += m;
    }
    return r;
}

// ---- drag from observations --------------------------------------------------------------------

struct DragEstimate {
    bool ok = false;
    float k = 0;     // linear drag per second
    int frames = 0;  // physics frames that passed between the two observations
};

// The exact linear drag of a shell from two observed velocities. The velocity only changes on physics frames, and an unknown whole
// number of frames n lies between two observations: for each n the horizontal velocity gives k = (1 - (vx1/vx0)^(1/n)) / h, and
// the vertical one tells which n is right (gravity adds g*h per frame). Returns ok = false when the pair does not fit the model
// (a collision, a field, thrust) or when the velocity did not change (same physics frame).
inline DragEstimate EstimateDragFrames(float vx0, float vy0, float vx1, float vy1, float gravityY) {
    DragEstimate best;
    if (!std::isfinite(vx0) || !std::isfinite(vy0) || !std::isfinite(vx1) || !std::isfinite(vy1) || !std::isfinite(gravityY)) return best;
    if (!(std::fabs(vx0) > 800.0f) || vx0 * vx1 <= 0.0f) return best;
    if (vx1 == vx0 && vy1 == vy0) return best;
    const double ratio = static_cast<double>(vx1) / static_cast<double>(vx0);
    if (!(ratio > 0.2) || ratio > 1.000001) return best;
    double bestResidual = 1.0e30;
    for (int n = 1; n <= 6; ++n) {
        const double q = std::pow(std::min(ratio, 1.0), 1.0 / n);
        const double k = (1.0 - q) / kSimFrame;
        const ShotState s = AdvanceFrames({0, 0, vx0, vy0}, gravityY, k, n);
        const double residual = std::fabs(s.vy - vy1) + std::fabs(s.vx - vx1);
        if (residual < bestResidual) {
            bestResidual = residual;
            best.k = static_cast<float>(k);
            best.frames = n;
        }
    }
    best.ok = bestResidual < 0.6;  // velocities are floats read straight from the game: the true residual is far smaller
    return best;
}

// ---- aim calculator -------------------------------------------------------------------------------

struct AimSolution {
    bool valid = false;
    float elevationDeg = 0;  // above the horizontal, towards the target (negative = aim below the horizon)
    float time = 0;          // seconds until the shot passes the target's x
    bool clear = true;       // the shot gets there without meeting terrain or water first (as judged by the caller's ClearFn)
};

// Elevations (at most two: the flattest and the highest) for which a shot of the given speed passes through `target`, on the game's
// frame physics. `clear(elevationRad, reachTime)` -> true when that shot gets to reachTime without hitting a surface (terrain / water).
// Returns the number of solutions written to out[0..1]. Not for beams (gravity 0).
template <class ClearFn>
int SolveAim(Vec2f origin, float speed, float gravityY, float drag, Vec2f target, ClearFn&& clear, AimSolution out[2]) {
    out[0] = out[1] = AimSolution{};
    if (!(speed > 1.0f) || !std::isfinite(speed) || !std::isfinite(gravityY) || std::fabs(gravityY) < 1.0e-3f || !(drag >= 0.0f)) return 0;
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(target.x) || !std::isfinite(target.y)) return 0;
    const double dx = static_cast<double>(target.x) - origin.x;
    if (std::fabs(dx) < 1.0) return 0;
    const double dirX = dx > 0 ? 1.0 : -1.0;
    const double up = gravityY >= 0.0f ? -1.0 : 1.0;  // world y direction of "up"
    const double g = gravityY, k = drag;
    const double yRef = up < 0 ? target.y : -static_cast<double>(target.y);  // "down is positive" height of the target
    constexpr int kMaxFrames = 600;                                          // 24 s of flight

    // Height (down is positive) of the shot where it crosses target.x (interpolated inside the frame), and the time it takes.
    auto atTarget = [&](double e, double& yDown, double& t) {
        ShotState st{origin.x, origin.y, dirX * speed * std::cos(e), up * speed * std::sin(e)};
        if (!(std::fabs(st.vx) > 1.0e-6)) return false;
        for (int f = 1; f <= kMaxFrames; ++f) {
            const double px = st.x, py = st.y;
            StepFrame(st, g, k);
            if ((static_cast<double>(target.x) - st.x) * dirX <= 0.0) {
                const double u = (static_cast<double>(target.x) - px) / (st.x - px);
                const double y = py + (st.y - py) * u;
                yDown = up < 0 ? y : -y;
                t = (f - 1 + u) * kSimFrame;
                return std::isfinite(yDown);
            }
            if (!(std::fabs(st.vx) > 1.0e-3)) return false;  // drag stopped it
        }
        return false;
    };

    constexpr double kDeg = 0.017453292519943295;
    std::vector<double> roots;
    double prevE = 0, prevF = 0;
    bool prevOk = false;
    for (double deg = -60.0; deg <= 89.0001; deg += 0.5) {
        const double e = deg * kDeg;
        double yDown = 0, t = 0;
        const bool ok = atTarget(e, yDown, t);
        const double f = ok ? yDown - yRef : 0.0;
        if (ok && prevOk && (prevF == 0.0 || f == 0.0 || (prevF < 0.0) != (f < 0.0))) {
            double lo = prevE, hi = e, flo = prevF;
            for (int it = 0; it < 48; ++it) {
                const double mid = 0.5 * (lo + hi);
                double ym = 0, tm = 0;
                if (!atTarget(mid, ym, tm)) break;
                const double fm = ym - yRef;
                if ((flo < 0.0) == (fm < 0.0)) { lo = mid; flo = fm; } else { hi = mid; }
            }
            const double root = 0.5 * (lo + hi);
            if (roots.empty() || std::fabs(root - roots.back()) > 1.0e-4) roots.push_back(root);
        }
        prevE = e;
        prevF = f;
        prevOk = ok;
    }
    if (roots.empty()) return 0;
    std::vector<double> chosen{roots.front()};
    if (roots.size() > 1) chosen.push_back(roots.back());
    int n = 0;
    for (double e : chosen) {
        double yDown = 0, t = 0;
        if (!atTarget(e, yDown, t)) continue;
        AimSolution s;
        s.valid = true;
        s.elevationDeg = static_cast<float>(e / kDeg);
        s.time = static_cast<float>(t);
        s.clear = clear(e, t);
        out[n++] = s;
    }
    return n;
}

// Elevation (degrees above the horizontal) of a velocity vector.
inline float ElevationDeg(Vec2f v, float gravityY) {
    const double up = gravityY >= 0.0f ? -1.0 : 1.0;
    return static_cast<float>(std::atan2(up * static_cast<double>(v.y), std::fabs(static_cast<double>(v.x))) * 57.29577951308232);
}

// Index of the point of `pts` nearest to `p` within `maxDist`, or -1. `dist` receives the distance.
inline int NearestWithin(Vec2f p, const std::vector<Vec2f>& pts, float maxDist, float& dist) {
    int best = -1;
    double bestD = maxDist;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const double d = detail::Dist(p, pts[i]);
        if (d <= bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    dist = best >= 0 ? static_cast<float>(bestD) : 0.0f;
    return best;
}

// ---- weak links of a base -------------------------------------------------------------------------

struct WeakLink {
    int edge = -1;        // index into the edge list
    double value = 0;     // summed weight of what is cut off from the ground when this link is lost
    int links = 0;        // number of links (this one included) that lose their ground connection
};

// A link is a weak link when it is a bridge of the support graph: its loss leaves a piece of the structure with no path to
// the ground. Nodes flagged `anchored` are the ground (merged into one root). `edgeWeight[i]` is what hangs on link i
// (devices attached to it). Results are sorted by value, then by number of links, and cut at `maxResults`.
// Floating pieces (not connected to the ground already) are ignored.
inline std::vector<WeakLink> FindWeakLinks(int nodeCount, const std::vector<char>& anchored, const std::vector<std::pair<int, int>>& edges,
                                           const std::vector<double>& edgeWeight, int maxResults) {
    std::vector<WeakLink> result;
    if (nodeCount <= 0 || edges.empty() || anchored.size() != static_cast<std::size_t>(nodeCount) || edgeWeight.size() != edges.size()) return result;
    const int root = nodeCount;  // virtual ground node
    const int V = nodeCount + 1;
    auto vertex = [&](int n) { return (n < 0 || n >= nodeCount) ? -1 : (anchored[static_cast<std::size_t>(n)] ? root : n); };

    struct Arc { int to, edge; };
    std::vector<std::vector<Arc>> adj(static_cast<std::size_t>(V));
    std::vector<std::pair<int, int>> ends(edges.size(), {-1, -1});
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const int a = vertex(edges[i].first), b = vertex(edges[i].second);
        if (a < 0 || b < 0 || a == b) continue;  // unknown node or a link between two ground points
        ends[i] = {a, b};
        adj[static_cast<std::size_t>(a)].push_back({b, static_cast<int>(i)});
        adj[static_cast<std::size_t>(b)].push_back({a, static_cast<int>(i)});
    }

    // Iterative DFS from the ground: discovery order, low-link, parent and the tree edge to the parent.
    std::vector<int> disc(static_cast<std::size_t>(V), -1), low(static_cast<std::size_t>(V), 0), parent(static_cast<std::size_t>(V), -1),
        parentEdge(static_cast<std::size_t>(V), -1), order;
    std::vector<std::size_t> next(static_cast<std::size_t>(V), 0);
    std::vector<int> stack{root};
    int timer = 0;
    disc[static_cast<std::size_t>(root)] = low[static_cast<std::size_t>(root)] = timer++;
    order.push_back(root);
    while (!stack.empty()) {
        const int v = stack.back();
        auto& it = next[static_cast<std::size_t>(v)];
        if (it < adj[static_cast<std::size_t>(v)].size()) {
            const Arc a = adj[static_cast<std::size_t>(v)][it++];
            if (a.edge == parentEdge[static_cast<std::size_t>(v)]) continue;
            if (disc[static_cast<std::size_t>(a.to)] < 0) {
                disc[static_cast<std::size_t>(a.to)] = low[static_cast<std::size_t>(a.to)] = timer++;
                parent[static_cast<std::size_t>(a.to)] = v;
                parentEdge[static_cast<std::size_t>(a.to)] = a.edge;
                order.push_back(a.to);
                stack.push_back(a.to);
            } else {
                low[static_cast<std::size_t>(v)] = std::min(low[static_cast<std::size_t>(v)], disc[static_cast<std::size_t>(a.to)]);
            }
        } else {
            stack.pop_back();
            const int p = parent[static_cast<std::size_t>(v)];
            if (p >= 0) low[static_cast<std::size_t>(p)] = std::min(low[static_cast<std::size_t>(p)], low[static_cast<std::size_t>(v)]);
        }
    }

    // Weight and link count of every link go to its endpoint that was discovered later: for a bridge that is always the
    // endpoint inside the cut-off piece (a non-tree link touching the piece cannot leave it, or the link would not be a bridge).
    std::vector<double> sumW(static_cast<std::size_t>(V), 0.0);
    std::vector<int> sumN(static_cast<std::size_t>(V), 0);
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto [a, b] = ends[i];
        if (a < 0 || disc[static_cast<std::size_t>(a)] < 0 || disc[static_cast<std::size_t>(b)] < 0) continue;
        const int lower = disc[static_cast<std::size_t>(a)] > disc[static_cast<std::size_t>(b)] ? a : b;
        sumW[static_cast<std::size_t>(lower)] += edgeWeight[i];
        sumN[static_cast<std::size_t>(lower)] += 1;
    }
    for (std::size_t i = order.size(); i-- > 1;) {  // children before parents
        const int v = order[i], p = parent[static_cast<std::size_t>(v)];
        if (p >= 0) {
            sumW[static_cast<std::size_t>(p)] += sumW[static_cast<std::size_t>(v)];
            sumN[static_cast<std::size_t>(p)] += sumN[static_cast<std::size_t>(v)];
        }
    }
    for (std::size_t i = 1; i < order.size(); ++i) {
        const int v = order[i], p = parent[static_cast<std::size_t>(v)];
        if (p < 0 || low[static_cast<std::size_t>(v)] <= disc[static_cast<std::size_t>(p)]) continue;  // not a bridge
        result.push_back({parentEdge[static_cast<std::size_t>(v)], sumW[static_cast<std::size_t>(v)], sumN[static_cast<std::size_t>(v)]});
    }
    std::sort(result.begin(), result.end(), [](const WeakLink& a, const WeakLink& b) {
        if (a.value != b.value) return a.value > b.value;
        if (a.links != b.links) return a.links > b.links;
        return a.edge < b.edge;
    });
    if (maxResults >= 0 && static_cast<int>(result.size()) > maxResults) result.resize(static_cast<std::size_t>(maxResults));
    return result;
}

// Which links lose their ground connection when link `removed` is destroyed: lost[i] = 1 for the removed link itself and for every
// link whose two ends are cut off from the ground by that loss (links between two ground points never are). Same graph model as
// FindWeakLinks; used to describe what a weak link takes down.
inline std::vector<char> LostLinks(int nodeCount, const std::vector<char>& anchored, const std::vector<std::pair<int, int>>& edges, int removed) {
    std::vector<char> lost(edges.size(), 0);
    if (nodeCount <= 0 || anchored.size() != static_cast<std::size_t>(nodeCount) || removed < 0 || removed >= static_cast<int>(edges.size())) return lost;
    const int root = nodeCount;
    auto vertex = [&](int n) { return (n < 0 || n >= nodeCount) ? -1 : (anchored[static_cast<std::size_t>(n)] ? root : n); };
    auto reach = [&](int skip) {
        std::vector<std::vector<int>> adj(static_cast<std::size_t>(nodeCount) + 1);
        for (std::size_t i = 0; i < edges.size(); ++i) {
            if (static_cast<int>(i) == skip) continue;
            const int a = vertex(edges[i].first), b = vertex(edges[i].second);
            if (a < 0 || b < 0 || a == b) continue;
            adj[static_cast<std::size_t>(a)].push_back(b);
            adj[static_cast<std::size_t>(b)].push_back(a);
        }
        std::vector<char> seen(static_cast<std::size_t>(nodeCount) + 1, 0);
        std::vector<int> stack{root};
        seen[static_cast<std::size_t>(root)] = 1;
        while (!stack.empty()) {
            const int v = stack.back();
            stack.pop_back();
            for (int o : adj[static_cast<std::size_t>(v)])
                if (!seen[static_cast<std::size_t>(o)]) {
                    seen[static_cast<std::size_t>(o)] = 1;
                    stack.push_back(o);
                }
        }
        return seen;
    };
    const std::vector<char> before = reach(-1), after = reach(removed);
    lost[static_cast<std::size_t>(removed)] = 1;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const int a = vertex(edges[i].first), b = vertex(edges[i].second);
        if (a < 0 || b < 0 || a == root || b == root) continue;
        if (before[static_cast<std::size_t>(a)] && before[static_cast<std::size_t>(b)] && !after[static_cast<std::size_t>(a)] && !after[static_cast<std::size_t>(b)]) lost[i] = 1;
    }
    return lost;
}

// Distance from point p to the segment a-b.
inline float DistanceToSegment(Vec2f p, Vec2f a, Vec2f b) {
    const double abx = static_cast<double>(b.x) - a.x, aby = static_cast<double>(b.y) - a.y;
    const double len2 = abx * abx + aby * aby;
    double u = len2 > 0.0 ? ((static_cast<double>(p.x) - a.x) * abx + (static_cast<double>(p.y) - a.y) * aby) / len2 : 0.0;
    u = std::clamp(u, 0.0, 1.0);
    return static_cast<float>(detail::Dist(p, {static_cast<float>(a.x + abx * u), static_cast<float>(a.y + aby * u)}));
}

}  // namespace forts
