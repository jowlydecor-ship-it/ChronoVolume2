// ChronoVolume - rendering core v2 (no OpenFX dependency).
// Original implementation. The clip's frames form a 3D "time volume" that is rendered by exact
// ray / plane compositing (no random sampling -> no noise), with supersampled anti-aliasing,
// surface shading, an optional studio floor (soft shadows + reflection), frosted panes and edge glow.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace chrono {

constexpr float kPi = 3.14159265358979f;
constexpr int kMaxFrames = 128;

// ---------------------------------------------------------------- parameters
struct Params {
    int mode = 1;            // 0 solid, 1 translucent, 2 separated slices, 3 cross section
    int style = 0;           // 0 smooth volume, 1 voxel dust (point cloud), 2 scan lines (lidar)
    int before = 24, after = 0;
    bool flip = false;       // front = oldest frame instead of newest
    bool loop = false;       // handled by the host layer
    bool blend = true;       // blend neighbouring frames (cheap smoothing, NOT optical flow)

    float depth = 1.5f;      // depth of the time block, in image heights
    float density = 3.f;     // translucent absorption density
    float emission = 1.f;    // translucent / dust emission (glow accumulation)
    float sliceOpacity = 0.9f;
    float trailFade = 0.3f;  // fades older frames (0..1)

    float yaw = 25.f, pitch = 15.f, roll = 0.f, fov = 35.f, zoom = 1.f;
    float posX = 0.f, posY = 0.f;     // move the cube on screen (fractions of frame width/height)
    float scaleW = 1.f, scaleH = 1.f; // cube width / height proportions
    float bend = 0.f;        // 0 = straight stack, 1 = closed ring
    int timeBase = 0;        // absolute frame number of the current time (keeps dust attached to its frame)
    int steps = 3;           // quality: sub-slices per frame gap (1..8)
    int aa = 2;              // anti-aliasing: samples per axis (1..4)

    float snap = 0.f;        // Snap to Front: 0 = 3D block, 1 = flat on the original video
    int motionMethod = 0;    // 0 background (temporal median), 1 difference with adjacent frames
    bool motionOnly = false;
    float motionThresh = 0.06f, motionSoft = 0.08f;

    int opacitySrc = 1;      // 0 uniform, 1 luma, 2 brightness(max), 3 saturation, 4 chroma key, 5 alpha
    float keyColor[3] = {0.f, 1.f, 0.f};
    float keyTol = 0.3f;
    float gain = 1.f;
    bool invert = false;

    int dotGrid = 60;        // voxel dust: cells per image height
    float dotSize = 0.7f, dotDensity = 0.35f;
    int lines = 48;          // scan lines per image height
    float lineWidth = 0.25f; // fraction of a line period that is lit

    int colorMode = 0;       // 0 source colours, 1 time age mapping
    float colorNew[3] = {1.f, 1.f, 1.f};   // present
    float colorMid[3] = {0.2f, 0.4f, 1.f}; // near past
    float colorOld[3] = {1.f, 0.2f, 0.2f}; // far past
    float colorMix = 0.8f;

    float crossPos = 0.5f, crossSlopeX = 1.f, crossSlopeY = 0.f;

    // look
    float shading = 0.6f;    // surface lighting + depth cue (0..1)
    float edgeGlow = 0.f;    // outline glow on panes / fresnel rim on solids
    float frost = 0.f;       // frosted glass: blur grows with depth (slices)
    bool floorOn = false;    // studio floor with soft shadow and reflection
    float floorColor[3] = {0.93f, 0.93f, 0.95f};
    float floorReflect = 0.25f;
    float shadowAmt = 0.55f;
    float shadowSoft = 0.5f;
    float lightYaw = -35.f, lightPitch = 50.f;

    float glowAmt = 0.f, glowRad = 12.f, glowThresh = 0.6f;
    float rolloff = 0.6f;    // highlight roll-off (soft clip, blown cores go white)
    float caAmt = 0.f;       // chromatic aberration, pixels at the image corner
    float grain = 0.f;

    float bg[4] = {0.f, 0.f, 0.f, 0.f}; // premultiplied background colour (when no floor)
};

// ---------------------------------------------------------------- utilities
struct V3 { float x, y, z; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 norm(V3 a) { float l = std::sqrt(dot(a, a)); return l > 0.f ? a * (1.f / l) : a; }

inline float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
inline float sstep(float a, float b, float x) { float t = clamp01((x - a) / (b - a)); return t * t * (3.f - 2.f * t); }

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
inline float rand01(int a, int b, int c = 0) {
    uint32_t h = hash32((uint32_t)a * 73856093u ^ hash32((uint32_t)b * 19349663u ^ hash32((uint32_t)c * 83492791u + 1u)));
    return (h >> 8) * (1.0f / 16777216.0f);
}

// Row-parallel helper with dynamic scheduling (the OFX multithread suite could be used instead).
inline void parallelRows(int y0, int y1, const std::function<void(int, int)>& fn) {
    if (y1 <= y0) return;
    const unsigned n = std::max(1u, std::thread::hardware_concurrency());
    const int total = y1 - y0;
    const int chunk = std::max(1, total / (int)(n * 8));
    std::atomic<int> next(y0);
    auto worker = [&]() {
        for (;;) {
            int a = next.fetch_add(chunk);
            if (a >= y1) break;
            fn(a, std::min(y1, a + chunk));
        }
    };
    unsigned nt = std::min<unsigned>(n, (unsigned)((total + chunk - 1) / chunk));
    std::vector<std::thread> th;
    for (unsigned i = 1; i < nt; i++) th.emplace_back(worker);
    worker();
    for (auto& t : th) t.join();
}

inline const float kZero4[4] = {0.f, 0.f, 0.f, 0.f};

// Read-only view on one float RGBA frame. Row 0 = bottom (OFX convention).
struct FrameView {
    const char* base = nullptr;
    std::ptrdiff_t rowBytes = 0;
    int w = 0, h = 0;

    inline const float* px(int x, int y) const {
        if (w <= 0 || h <= 0) return kZero4;
        x = x < 0 ? 0 : (x >= w ? w - 1 : x);
        y = y < 0 ? 0 : (y >= h ? h - 1 : y);
        return reinterpret_cast<const float*>(base + (std::ptrdiff_t)y * rowBytes + (std::ptrdiff_t)x * 16);
    }
    inline void bilinear(float u, float v, float o[4]) const {
        if (w <= 0 || h <= 0) { o[0] = o[1] = o[2] = o[3] = 0.f; return; }
        float fx = u * w - 0.5f, fy = v * h - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        float tx = fx - x0, ty = fy - y0;
        const float *a = px(x0, y0), *b = px(x0 + 1, y0), *c = px(x0, y0 + 1), *d = px(x0 + 1, y0 + 1);
        for (int k = 0; k < 4; k++)
            o[k] = (a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty;
    }
};

// Separable box blur on RGB of an RGBA float buffer (edge-clamped).
inline void blurAxis(const std::vector<float>& in, std::vector<float>& out, int w, int h, int r, bool horiz) {
    const int len = horiz ? w : h, lines = horiz ? h : w;
    const float inv = 1.f / (2 * r + 1);
    parallelRows(0, lines, [&](int a, int b) {
        for (int l = a; l < b; l++) {
            auto at = [&](int i) -> const float* {
                i = std::min(std::max(i, 0), len - 1);
                return &in[(horiz ? (size_t)l * w + i : (size_t)i * w + l) * 4];
            };
            float s[3] = {0, 0, 0};
            for (int i = -r; i <= r; i++) { const float* p = at(i); s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; }
            for (int i = 0; i < len; i++) {
                float* o = &out[(horiz ? (size_t)l * w + i : (size_t)i * w + l) * 4];
                o[0] = s[0] * inv; o[1] = s[1] * inv; o[2] = s[2] * inv; o[3] = 0.f;
                const float *pa = at(i + r + 1), *pr = at(i - r);
                s[0] += pa[0] - pr[0]; s[1] += pa[1] - pr[1]; s[2] += pa[2] - pr[2];
            }
        }
    });
}

// ---------------------------------------------------------------- renderer
class Renderer {
public:
    Renderer(const Params& p, const std::vector<FrameView>& f, int w, int h, float renderScale, uint32_t seed)
        : P(p), frames(f), N((int)f.size()), outW(w), outH(h), rs(renderScale), seed((int)seed) {}

    // out: outW*outH*4 floats, premultiplied RGBA, row 0 = bottom. (No finishing pass: see postProcess.)
    void run(std::vector<float>& out) {
        out.assign((size_t)outW * outH * 4, 0.f);
        if (N <= 0 || outW <= 0 || outH <= 0) return;
        prepare();
        const size_t npx = (size_t)outW * outH;

        // 1. one ray per pixel
        std::vector<float> vol(npx * 4, 0.f);
        parallelRows(0, outH, [&](int a, int b) {
            for (int y = a; y < b; y++)
                for (int x = 0; x < outW; x++) {
                    float* o = &vol[((size_t)y * outW + x) * 4];
                    traceVolume(cam, rayDir((x + 0.5f) / outW, (y + 0.5f) / outH), false, o, o[3]);
                }
        });

        // 2. adaptive anti-aliasing: only pixels on an edge get extra samples
        const int g = std::max(1, std::min(4, P.aa));
        if (g > 1) {
            std::vector<float> v2 = vol;
            parallelRows(0, outH, [&](int a, int b) {
                for (int y = a; y < b; y++)
                    for (int x = 0; x < outW; x++) {
                        if (!edgeAt(vol, x, y)) continue;
                        float acc[4] = {0, 0, 0, 0};
                        for (int sy = 0; sy < g; sy++)
                            for (int sx = 0; sx < g; sx++) {
                                float C[3], A;
                                traceVolume(cam, rayDir((x + (sx + 0.5f) / g) / outW, (y + (sy + 0.5f) / g) / outH), false, C, A);
                                acc[0] += C[0]; acc[1] += C[1]; acc[2] += C[2]; acc[3] += A;
                            }
                        float* o = &v2[((size_t)y * outW + x) * 4];
                        for (int k = 0; k < 4; k++) o[k] = acc[k] / (float)(g * g);
                    }
            });
            vol.swap(v2);
        }

        // 3. studio floor (shadow + reflection) on a coarse grid, interpolated per pixel
        const int cell = 3;
        const int gw = (outW + cell - 1) / cell + 2, gh = (outH + cell - 1) / cell + 2;
        std::vector<float> fgrid;
        if (floorActive) {
            fgrid.assign((size_t)gw * gh * 3, 0.f);
            parallelRows(0, gh, [&](int a, int b) {
                for (int gy = a; gy < b; gy++)
                    for (int gx = 0; gx < gw; gx++) {
                        int px = std::min(gx * cell, outW - 1), py = std::min(gy * cell, outH - 1);
                        float maxFree = 0.f;
                        const int ox[5] = {0, cell, -cell, 0, 0}, oy[5] = {0, 0, 0, cell, -cell};
                        for (int k = 0; k < 5; k++) {
                            int qx = std::min(std::max(px + ox[k], 0), outW - 1), qy = std::min(std::max(py + oy[k], 0), outH - 1);
                            maxFree = std::max(maxFree, 1.f - vol[((size_t)qy * outW + qx) * 4 + 3]);
                        }
                        float* F = &fgrid[((size_t)gy * gw + gx) * 3];
                        if (maxFree < 0.004f) { F[0] = P.floorColor[0]; F[1] = P.floorColor[1]; F[2] = P.floorColor[2]; continue; }
                        floorColor(rayDir((px + 0.5f) / outW, (py + 0.5f) / outH), px, py, F);
                    }
            });
        }

        // 4. composite the volume over the floor / background
        parallelRows(0, outH, [&](int a, int b) {
            for (int y = a; y < b; y++)
                for (int x = 0; x < outW; x++) {
                    const float* v = &vol[((size_t)y * outW + x) * 4];
                    float F[3] = {P.bg[0], P.bg[1], P.bg[2]}, Fa = P.bg[3];
                    if (floorActive) {
                        Fa = 1.f;
                        float fx = (float)x / cell, fy = (float)y / cell;
                        int x0 = (int)fx, y0 = (int)fy;
                        float tx = fx - x0, ty = fy - y0;
                        const float *p00 = &fgrid[((size_t)y0 * gw + x0) * 3], *p10 = p00 + 3,
                                    *p01 = &fgrid[((size_t)(y0 + 1) * gw + x0) * 3], *p11 = p01 + 3;
                        for (int k = 0; k < 3; k++)
                            F[k] = (p00[k] * (1 - tx) + p10[k] * tx) * (1 - ty) + (p01[k] * (1 - tx) + p11[k] * tx) * ty;
                    }
                    float* o = &out[((size_t)y * outW + x) * 4];
                    float free_ = 1.f - v[3];
                    o[0] = v[0] + free_ * F[0];
                    o[1] = v[1] + free_ * F[1];
                    o[2] = v[2] + free_ * F[2];
                    o[3] = v[3] + free_ * Fa;
                }
        });
    }

private:
    Params P;
    const std::vector<FrameView>& frames;
    int N, outW, outH;
    float rs;
    int seed;

    // scene
    float W = 1.78f, Wc = 1.78f, Hc = 1.f, D = 1.5f, Rc = 1.f, theta = 0.f, tanV = 0.3f, tanH = 0.5f, rad = 1.f;
    bool bent = false, floorActive = false;
    float floorY = -0.5f;
    V3 cam{0, 0, -3}, fwd{0, 0, 1}, right{1, 0, 0}, up{0, 1, 0}, T{0, 0, 0}, L{0, 1, 0};
    float avgCol[3] = {0.5f, 0.5f, 0.5f};
    int refW = 1, refH = 1;
    std::vector<float> bg; // temporal median background (RGB) for Motion Only
    int bgW = 0, bgH = 0;

    // ------------------------------------------------------------ setup
    void prepare() {
        if (P.snap > 0.f) { // pull the whole structure back onto the flat video
            float k = 1.f - clamp01(P.snap);
            P.yaw *= k; P.pitch *= k; P.roll *= k; P.posX *= k; P.posY *= k; P.bend *= k;
            P.scaleW = 1.f + (P.scaleW - 1.f) * k; P.scaleH = 1.f + (P.scaleH - 1.f) * k;
            P.zoom = 1.f + (P.zoom - 1.f) * k;
            P.depth = std::max(0.05f, P.depth * k);
        }
        if (P.mode == 3) P.bend = 0.f; // the cross-section plane lives in the straight block

        int fw = 0, fh = 0;
        for (auto& f : frames) if (f.w > 0) { fw = f.w; fh = f.h; break; }
        refW = std::max(1, fw); refH = std::max(1, fh);
        W = fh > 0 ? (float)fw / (float)fh : (float)outW / (float)outH;
        D = std::max(0.05f, P.depth);
        Wc = W * std::max(0.05f, P.scaleW);
        Hc = std::max(0.05f, P.scaleH);

        float halfFov = std::max(2.f, std::min(120.f, P.fov)) * 0.5f * kPi / 180.f;
        tanV = std::tan(halfFov);
        tanH = tanV * (float)outW / (float)outH;
        float zoom = std::max(0.05f, P.zoom);

        bent = P.bend > 0.002f;
        theta = P.bend * 2.f * kPi;
        float R;
        if (!bent) {
            T = {0, 0, 0};
            rad = 0.5f * std::sqrt(Wc * Wc + Hc * Hc + D * D);
            // cross section: the middle plane fills the frame; otherwise the front face does
            R = (0.5f * Hc / tanV + (P.mode == 3 ? 0.f : D * 0.5f)) / zoom;
        } else {
            Rc = std::max(D / theta, 0.55f * Wc);
            float k = std::min(1.f, theta / (2.f * kPi));
            T = {-Rc * k, 0, 0};
            float hx = Rc + Wc * 0.5f + Rc * (1.f - k);
            rad = std::sqrt(hx * hx + 0.25f * Hc * Hc);
            R = rad / std::sin(halfFov) / zoom;
        }
        float yaw = P.yaw * kPi / 180.f;
        float pit = std::max(-89.f, std::min(89.f, P.pitch)) * kPi / 180.f;
        V3 dir{std::sin(yaw) * std::cos(pit), std::sin(pit), -std::cos(yaw) * std::cos(pit)};
        cam = T + dir * R;
        fwd = dir * -1.f;
        right = norm(cross({0, 1, 0}, fwd));
        up = cross(fwd, right);
        {
            float rl = P.roll * kPi / 180.f, cr = std::cos(rl), sr = std::sin(rl);
            V3 r2 = right * cr + up * sr, u2 = up * cr - right * sr;
            right = r2; up = u2;
        }

        float la = P.lightYaw * kPi / 180.f, le = std::max(5.f, std::min(89.f, P.lightPitch)) * kPi / 180.f;
        L = norm({std::sin(la) * std::cos(le), std::sin(le), -std::cos(la) * std::cos(le)});
        floorY = -Hc * 0.5f;
        floorActive = P.floorOn && cam.y > floorY + 1e-3f;

        if (fw > 0) { // average colour of the front frame: used for light bleeding on the floor
            double s[3] = {0, 0, 0};
            for (int j = 0; j < 8; j++)
                for (int i = 0; i < 8; i++) {
                    float c[4];
                    frames[0].bilinear((i + 0.5f) / 8.f, (j + 0.5f) / 8.f, c);
                    s[0] += c[0]; s[1] += c[1]; s[2] += c[2];
                }
            for (int k = 0; k < 3; k++) avgCol[k] = (float)(s[k] / 64.0);
        }

        if (P.motionOnly && P.motionMethod == 0 && fw > 0) {
            const int stride = fw >= 640 ? 2 : 1;
            bgW = (fw + stride - 1) / stride; bgH = (fh + stride - 1) / stride;
            bg.assign((size_t)bgW * bgH * 3, 0.f);
            const int K = std::max(1, std::min(N, 9));
            int idx[9];
            for (int i = 0; i < K; i++) idx[i] = K > 1 ? (int)std::lround(i * (N - 1) / (float)(K - 1)) : 0;
            parallelRows(0, bgH, [&](int a, int b) {
                float tmp[9];
                for (int y = a; y < b; y++)
                    for (int x = 0; x < bgW; x++)
                        for (int c = 0; c < 3; c++) {
                            for (int k = 0; k < K; k++) tmp[k] = frames[idx[k]].px(x * stride, y * stride)[c];
                            std::nth_element(tmp, tmp + K / 2, tmp + K);
                            bg[((size_t)y * bgW + x) * 3 + c] = tmp[K / 2];
                        }
            });
        }
    }

    V3 rayDir(float px, float py) const {
        float nx = px * 2.f - 1.f - 2.f * P.posX, ny = py * 2.f - 1.f - 2.f * P.posY;
        return norm(fwd + right * (nx * tanH) + up * (ny * tanV));
    }

    // ------------------------------------------------------------ frame sampling
    void sampleFrames(float j, float u, float v, float c[4]) const {
        if (!P.blend || N == 1) {
            int i = std::min(std::max((int)std::lround(j), 0), N - 1);
            frames[i].bilinear(u, v, c);
            return;
        }
        j = std::min(std::max(j, 0.f), (float)(N - 1));
        int i0 = (int)std::floor(j), i1 = std::min(i0 + 1, N - 1);
        float t = j - i0;
        frames[i0].bilinear(u, v, c);
        if (t > 0.001f && i1 != i0) {
            float b[4];
            frames[i1].bilinear(u, v, b);
            for (int k = 0; k < 4; k++) c[k] = c[k] * (1.f - t) + b[k] * t;
        }
    }

    // 9-tap soft blur used for frosted glass (radius in world units)
    void sampleFramesBlur(float j, float u, float v, float rWorld, float c[4]) const {
        float du = rWorld / Wc, dv = rWorld / Hc;
        float acc[4] = {0, 0, 0, 0}, t[4];
        static const float ox[9] = {0.f, 1.f, -1.f, 0.f, 0.f, 0.7f, -0.7f, 0.7f, -0.7f};
        static const float oy[9] = {0.f, 0.f, 0.f, 1.f, -1.f, 0.7f, 0.7f, -0.7f, -0.7f};
        for (int k = 0; k < 9; k++) {
            sampleFrames(j, u + ox[k] * du, v + oy[k] * dv, t);
            for (int q = 0; q < 4; q++) acc[q] += t[q];
        }
        for (int q = 0; q < 4; q++) c[q] = acc[q] * (1.f / 9.f);
    }

    float opacityOf(const float c[4], float u, float v, float j) const {
        float m = 1.f;
        switch (P.opacitySrc) {
            case 1: m = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; break;
            case 2: m = std::max(c[0], std::max(c[1], c[2])); break;
            case 3: {
                float mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2]));
                m = (mx - mn) / (mx + 1e-5f);
                break;
            }
            case 4: {
                float dr = c[0] - P.keyColor[0], dg = c[1] - P.keyColor[1], db = c[2] - P.keyColor[2];
                m = sstep(0.f, std::max(0.01f, P.keyTol), std::sqrt(dr * dr + dg * dg + db * db));
                break;
            }
            case 5: m = c[3]; break;
            default: m = 1.f;
        }
        m = clamp01(m * P.gain);
        if (P.invert) m = 1.f - m;
        if (P.motionOnly) {
            float d = 0.f;
            if (P.motionMethod == 1) { // difference with the adjacent frames of the stack
                int i = std::min(std::max((int)std::lround(j), 0), N - 1);
                for (int o = -1; o <= 1; o += 2) {
                    int k = i + o;
                    if (k < 0 || k >= N) continue;
                    float a[4];
                    frames[k].bilinear(u, v, a);
                    d = std::max(d, std::max(std::fabs(c[0] - a[0]), std::max(std::fabs(c[1] - a[1]), std::fabs(c[2] - a[2]))));
                }
            } else if (bgW > 0) { // bilinear lookup so the mask matches the filtered frame samples
                float fx = u * bgW - 0.5f, fy = v * bgH - 0.5f;
                int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                float tx = fx - x0, ty = fy - y0;
                auto B = [&](int x, int y, int ch) {
                    x = std::min(std::max(x, 0), bgW - 1); y = std::min(std::max(y, 0), bgH - 1);
                    return bg[((size_t)y * bgW + x) * 3 + ch];
                };
                for (int ch = 0; ch < 3; ch++) {
                    float b = (B(x0, y0, ch) * (1 - tx) + B(x0 + 1, y0, ch) * tx) * (1 - ty) +
                              (B(x0, y0 + 1, ch) * (1 - tx) + B(x0 + 1, y0 + 1, ch) * tx) * ty;
                    d = std::max(d, std::fabs(c[ch] - b));
                }
            }
            m *= sstep(P.motionThresh, P.motionThresh + P.motionSoft + 1e-4f, d);
        }
        return m;
    }

    // colour + opacity of the volume at (u,v,j); false = empty. w = normalised time position.
    bool evalVolume(float u, float v, float j, float w, float rgb[3], float& m, bool lite) const {
        float c[4];
        if (P.style == 1) { // voxel dust (soft round particles)
            float Gv = (float)std::max(4, P.dotGrid), Gu = Gv * Wc, Gv2 = Gv * Hc;
            float su = u * Gu, sv = v * Gv2;
            int iu = (int)std::floor(su), iv = (int)std::floor(sv), jc = (int)std::lround(j);
            const int fid = P.timeBase + (P.flip ? (jc - P.before) : (P.after - jc)); // stays attached to its video frame
            if (rand01(iu, iv, fid) > P.dotDensity) return false;
            float du = su - iu - 0.5f - (rand01(iu, iv, fid * 3 + 1) - 0.5f) * 0.5f;
            float dv = sv - iv - 0.5f - (rand01(iu, iv, fid * 3 + 2) - 0.5f) * 0.5f;
            float r = P.dotSize * 0.5f, dd = std::sqrt(du * du + dv * dv);
            if (dd > r) return false;
            float uc = (iu + 0.5f) / Gu, vc = (iv + 0.5f) / Gv2;
            sampleFrames((float)jc, uc, vc, c);
            m = opacityOf(c, uc, vc, (float)jc) * (1.f - sstep(0.55f * r, r, dd));
        } else {
            if (P.style == 2) { // scan lines (soft edges)
                float f = v * (float)std::max(1, P.lines);
                f -= std::floor(f);
                if (f > P.lineWidth) return false;
            }
            if (P.frost > 0.f && !lite && P.style == 0) sampleFramesBlur(j, u, v, P.frost * 0.05f * w, c);
            else sampleFrames(j, u, v, c);
            m = opacityOf(c, u, v, j);
        }
        if (m <= 0.f) return false;
        rgb[0] = c[0]; rgb[1] = c[1]; rgb[2] = c[2];
        return true;
    }

    // ------------------------------------------------------------ volume geometry
    bool toVolume(const V3& p, float& u, float& v, float& w) const {
        v = p.y / Hc + 0.5f;
        if (!bent) {
            u = p.x / Wc + 0.5f;
            w = p.z / D + 0.5f;
        } else {
            float rx = p.x + Rc, rz = p.z;
            float r = std::sqrt(rx * rx + rz * rz);
            u = (r - Rc) / Wc + 0.5f;
            w = std::atan2(rz, rx) / theta + 0.5f;
        }
        return v >= 0.f && v <= 1.f && u >= 0.f && u <= 1.f && w >= -1e-4f && w <= 1.f + 1e-4f;
    }

    bool hitBox(const V3& o, const V3& d, float& t0, float& t1) const {
        t0 = -1e30f; t1 = 1e30f;
        const float bmin[3] = {-Wc * 0.5f, -Hc * 0.5f, -D * 0.5f}, bmax[3] = {Wc * 0.5f, Hc * 0.5f, D * 0.5f};
        const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
        for (int i = 0; i < 3; i++) {
            if (std::fabs(dd[i]) < 1e-8f) { if (oo[i] < bmin[i] || oo[i] > bmax[i]) return false; continue; }
            float a = (bmin[i] - oo[i]) / dd[i], b = (bmax[i] - oo[i]) / dd[i];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a); t1 = std::min(t1, b);
            if (t0 > t1) return false;
        }
        return t1 > 0.f;
    }

    // outward normal of the volume face nearest to (u,v,w)
    V3 normalAt(float u, float v, float w) const {
        float du0 = u * Wc, du1 = (1.f - u) * Wc, dv0 = v * Hc, dv1 = (1.f - v) * Hc;
        float arc = bent ? (Rc + (u - 0.5f) * Wc) * theta : D;
        float dw0 = w * arc, dw1 = (1.f - w) * arc;
        float m = std::min(std::min(std::min(du0, du1), std::min(dv0, dv1)), std::min(dw0, dw1));
        if (!bent) {
            if (m == du0) return {-1, 0, 0};
            if (m == du1) return {1, 0, 0};
            if (m == dv0) return {0, -1, 0};
            if (m == dv1) return {0, 1, 0};
            if (m == dw0) return {0, 0, -1};
            return {0, 0, 1};
        }
        float phi = (w - 0.5f) * theta;
        V3 er{std::cos(phi), 0, std::sin(phi)}, et{-std::sin(phi), 0, std::cos(phi)};
        if (m == du0) return er * -1.f;
        if (m == du1) return er;
        if (m == dv0) return {0, -1, 0};
        if (m == dv1) return {0, 1, 0};
        if (m == dw0) return et * -1.f;
        return et;
    }

    void tint(float rgb[3], float w) const {
        if (P.colorMode != 1) return;
        for (int k = 0; k < 3; k++) {
            float t = w < 0.5f ? P.colorNew[k] + (P.colorMid[k] - P.colorNew[k]) * (w * 2.f)
                               : P.colorMid[k] + (P.colorOld[k] - P.colorMid[k]) * ((w - 0.5f) * 2.f);
            rgb[k] = rgb[k] * (1.f - P.colorMix) + t * P.colorMix;
        }
    }

    bool solidUnmasked() const {
        return P.mode == 0 && P.style == 0 && !P.motionOnly && P.opacitySrc == 0 && !P.invert && P.gain > 0.5f;
    }

    // ------------------------------------------------------------ tracing
    // Returns premultiplied colour C and alpha A of the volume along the ray. shadow = occlusion only.
    void traceVolume(const V3& o, const V3& d, bool shadow, float C[3], float& A) const {
        C[0] = C[1] = C[2] = 0.f; A = 0.f;
        if (P.mode == 3) { crossSection(o, d, shadow, C, A); return; }
        if (solidUnmasked()) { solidSurface(o, d, shadow, C, A); return; }

        const bool cards = (P.mode == 2 || P.style != 0);
        const int Sreq = std::max(1, std::min(8, P.steps));
        const int S = cards ? 1 : (shadow ? std::min(2, Sreq) : Sreq);
        const int Nq = N > 1 ? (N - 1) * S + 1 : 1;
        const float denomQ = (float)std::max(1, Nq - 1);

        if (!bent) {
            float t0, t1;
            if (!hitBox(o, d, t0, t1) || std::fabs(d.z) < 1e-7f) return;
            t0 = std::max(t0, 0.f);
            float w0 = (o.z + d.z * t0) / D + 0.5f, w1 = (o.z + d.z * t1) / D + 0.5f;
            float wl = std::min(w0, w1), wh = std::max(w0, w1);
            int qa = 0, qb = 0;
            if (Nq > 1) {
                qa = std::max(0, (int)std::ceil(wl * denomQ - 1e-4f));
                qb = std::min(Nq - 1, (int)std::floor(wh * denomQ + 1e-4f));
            } else { qa = qb = 0; }
            if (qa > qb) return;
            const bool asc = d.z > 0.f;
            const V3 n{0, 0, 1};
            for (int k = 0; k <= qb - qa; k++) {
                int q = asc ? qa + k : qb - k;
                float w = Nq > 1 ? (float)q / denomQ : 0.5f;
                float t = ((w - 0.5f) * D - o.z) / d.z;
                if (t <= 0.f) continue;
                addPlane(o, d, t, w, denomQ, n, cards, shadow, C, A);
                if (A > 0.995f) break;
            }
        } else {
            struct Hit { float t; float w; };
            Hit hits[kMaxFrames * 8 + 1];
            int nh = 0;
            for (int q = 0; q < Nq; q++) {
                float w = Nq > 1 ? (float)q / denomQ : 0.5f;
                float phi = (w - 0.5f) * theta;
                V3 n{-std::sin(phi), 0, std::cos(phi)};
                float den = dot(d, n);
                if (std::fabs(den) < 1e-6f) continue;
                float t = dot(V3{-Rc, 0, 0} - o, n) / den;
                if (t <= 0.f) continue;
                hits[nh++] = {t, w};
            }
            std::sort(hits, hits + nh, [](const Hit& a, const Hit& b) { return a.t < b.t; });
            for (int h = 0; h < nh; h++) {
                float phi = (hits[h].w - 0.5f) * theta;
                V3 n{-std::sin(phi), 0, std::cos(phi)};
                addPlane(o, d, hits[h].t, hits[h].w, denomQ, n, cards, shadow, C, A);
                if (A > 0.995f) break;
            }
        }
    }

    // Lit relief for masked solids (motion sculptures): normal from the opacity gradient of the volume.
    void shadeSculpture(float u, float v, float jf, const V3& d, float rgb[3]) const {
        auto mv = [&](float uu, float vv, float jj) {
            float c[3], mm;
            jj = std::min(std::max(jj, 0.f), (float)(N - 1));
            float ww = N > 1 ? jj / (float)(N - 1) : 0.5f;
            return evalVolume(uu, vv, jj, ww, c, mm, true) ? mm : 0.f;
        };
        float du = 1.5f / (float)refW, dv = 1.5f / (float)refH, dj = 0.6f;
        float gx = (mv(u + du, v, jf) - mv(u - du, v, jf)) / (2.f * du * Wc);
        float gy = (mv(u, v + dv, jf) - mv(u, v - dv, jf)) / (2.f * dv * Hc);
        float gz = (mv(u, v, jf + dj) - mv(u, v, jf - dj)) / (2.f * dj) * (float)std::max(1, N - 1) / D;
        V3 n{-gx, -gy, -gz};
        float len = std::sqrt(dot(n, n));
        n = len < 1e-4f ? V3{0, 0, -1} : norm(n * (1.f / len) + V3{0, 0, -0.35f});
        float s = P.shading;
        float ndl = std::max(0.f, dot(n, L));
        float lit = (1.f - s) + s * (0.35f + 0.65f * ndl);
        V3 H = norm(L - d);
        float spec = std::pow(std::max(0.f, dot(n, H)), 40.f) * 0.35f * s;
        float rim = std::pow(1.f - std::fabs(dot(n, d)), 3.f) * P.edgeGlow * 0.4f;
        for (int k = 0; k < 3; k++) rgb[k] = rgb[k] * lit + spec + rim * (0.5f + 0.5f * rgb[k]);
    }

    // One time-plane crossed by the ray: sample, shade and composite front-to-back.
    void addPlane(const V3& o, const V3& d, float t, float w, float denomQ, const V3& n, bool cards, bool shadow,
                  float C[3], float& A) const {
        V3 p = o + d * t;
        float u, v, w2;
        if (!toVolume(p, u, v, w2)) return;
        if (bent && std::fabs(w2 - w) > 2e-3f) return;
        const float jf = w * (float)(N - 1);
        float rgb[3], m;
        if (!evalVolume(u, v, jf, w, rgb, m, shadow)) return;
        m *= 1.f - P.trailFade * w;

        float a, thick = 0.f;
        if (cards) a = clamp01(m * P.sliceOpacity);
        else if (P.mode == 0) a = sstep(0.25f, 0.6f, m);
        else {
            float spacing = (bent ? std::sqrt((p.x + Rc) * (p.x + Rc) + p.z * p.z) * theta : D) / denomQ;
            float cosT = std::max(0.08f, std::fabs(dot(d, n)));
            thick = std::min(spacing / cosT, spacing * 8.f);
            a = 1.f - std::exp(-P.density * m * thick);
        }
        if (shadow) { A += (1.f - A) * a; return; }

        tint(rgb, w);
        if (P.mode == 0 && !cards && !bent && a > 0.15f && P.shading > 0.f) shadeSculpture(u, v, jf, d, rgb);
        const float cue = 1.f - 0.45f * P.shading * w; // soft depth cue / ambient occlusion feel
        float Cp[3] = {a * rgb[0] * cue, a * rgb[1] * cue, a * rgb[2] * cue}, ap = a;

        // emission: bright regions accumulate light (white-hot cores), like a glowing medium
        float emit = 0.f;
        if (cards && P.style != 0) emit = P.emission * m * 0.6f;
        else if (!cards && P.mode == 1) emit = P.emission * m * thick;
        if (emit > 0.f) {
            for (int k = 0; k < 3; k++) Cp[k] += emit * rgb[k] * cue;
            ap = a + (1.f - a) * std::min(1.f, emit);
        }

        if (P.mode == 2 && P.style == 0 && P.edgeGlow > 0.f) { // neon-like outline of each pane
            float ed = std::min(std::min(u * Wc, (1.f - u) * Wc), std::min(v * Hc, (1.f - v) * Hc));
            float eg = P.edgeGlow * std::exp(-ed / 0.012f) * (1.f - 0.6f * w);
            if (eg > 0.002f) {
                float ae = std::min(1.f, eg);
                for (int k = 0; k < 3; k++) Cp[k] += (1.f - ap) * ae * (rgb[k] * 0.5f + 0.5f);
                ap = ap + (1.f - ap) * ae;
            }
        }
        for (int k = 0; k < 3; k++) C[k] += (1.f - A) * Cp[k];
        A += (1.f - A) * ap;
    }

    // Opaque block with exact surface hit (front face = current frame, sides = slit-scan).
    void solidSurface(const V3& o, const V3& d, bool shadow, float C[3], float& A) const {
        float u = 0, v = 0, w = 0;
        V3 p{0, 0, 0};
        if (!bent) {
            float t0, t1;
            if (!hitBox(o, d, t0, t1) || t0 <= 0.f) return;
            p = o + d * t0;
        } else {
            V3 oc = o - T;
            float b = dot(oc, d), c = dot(oc, oc) - rad * rad, disc = b * b - c;
            if (disc < 0.f) return;
            float s = std::sqrt(disc), ta = std::max(0.f, -b - s), tb = -b + s;
            if (tb <= ta) return;
            const int ST = 192;
            float dt = (tb - ta) / ST, prev = ta, tin = -1.f;
            for (int i = 1; i <= ST; i++) {
                float t = ta + dt * i;
                float uu, vv, ww;
                if (toVolume(o + d * t, uu, vv, ww)) {
                    float lo = prev, hi = t;
                    for (int it = 0; it < 10; it++) {
                        float mid = 0.5f * (lo + hi);
                        if (toVolume(o + d * mid, uu, vv, ww)) hi = mid; else lo = mid;
                    }
                    tin = hi;
                    break;
                }
                prev = t;
            }
            if (tin < 0.f) return;
            p = o + d * tin;
        }
        toVolume(p, u, v, w);
        u = clamp01(u); v = clamp01(v); w = clamp01(w);
        A = 1.f;
        if (shadow) return;

        V3 n = normalAt(u, v, w);
        float c4[4];
        sampleFrames(w * (float)(N - 1), u, v, c4);
        float rgb[3] = {c4[0], c4[1], c4[2]};
        tint(rgb, w);
        float ndl = std::max(0.f, dot(n, L));
        float lit = (1.f - P.shading) + P.shading * (0.40f + 0.60f * ndl);
        float rim = std::pow(1.f - std::fabs(dot(n, d)), 2.5f) * P.edgeGlow * 0.5f;
        for (int k = 0; k < 3; k++) C[k] = rgb[k] * lit + rim * (0.5f + 0.5f * rgb[k]);
    }

    // Cross section: a slanted plane through the time stack, rendered in 3D.
    void crossSection(const V3& o, const V3& d, bool shadow, float C[3], float& A) const {
        float den = d.z - D * P.crossSlopeX * d.x / Wc - D * P.crossSlopeY * d.y / Hc;
        if (std::fabs(den) < 1e-7f) return;
        float rhs = D * (P.crossPos - 0.5f) + D * P.crossSlopeX * o.x / Wc + D * P.crossSlopeY * o.y / Hc - o.z;
        float t = rhs / den;
        if (t <= 0.f) return;
        float u, v, w;
        if (!toVolume(o + d * t, u, v, w)) return;
        float c[4];
        sampleFrames(clamp01(w) * (float)(N - 1), u, v, c);
        float a = P.motionOnly ? opacityOf(c, u, v, clamp01(w) * (float)(N - 1)) : 1.f;
        if (a <= 0.f) return;
        A = a;
        if (shadow) return;
        float rgb[3] = {c[0], c[1], c[2]};
        tint(rgb, clamp01(w));
        for (int k = 0; k < 3; k++) C[k] = rgb[k] * a;
    }

    // ------------------------------------------------------------ studio floor
    float shadowAt(const V3& p, int x, int y) const {
        const int K = 6;
        V3 t1 = norm(cross(L, std::fabs(L.y) > 0.9f ? V3{1, 0, 0} : V3{0, 1, 0}));
        V3 t2 = cross(L, t1);
        float size = 0.04f + 0.30f * P.shadowSoft;
        float ign = std::fmod(52.9829189f * std::fmod(0.06711056f * (float)x + 0.00583715f * (float)y, 1.f), 1.f);
        float rot = ign * 2.f * kPi, sum = 0.f;
        for (int k = 0; k < K; k++) {
            float r = std::sqrt((k + 0.5f) / K) * size, a = rot + k * 2.39996f;
            V3 dl = norm(L + t1 * (std::cos(a) * r) + t2 * (std::sin(a) * r));
            float C[3], A;
            traceVolume(p + L * 1e-3f, dl, true, C, A);
            sum += A;
        }
        return sum / K;
    }

    void floorColor(const V3& d, int x, int y, float out[3]) const {
        const float* base = P.floorColor;
        for (int k = 0; k < 3; k++) out[k] = base[k];
        if (d.y >= -1e-4f) return; // above the horizon: plain studio colour
        float t = (floorY - cam.y) / d.y;
        if (t <= 0.f) return;
        V3 p = cam + d * t;
        float dx = p.x - T.x, dz = p.z - T.z;
        float pool = std::exp(-(dx * dx + dz * dz) / (3.f * rad * rad));
        float col[3];
        float sh = P.shadowAmt > 0.f ? shadowAt(p, x, y) : 0.f;
        for (int k = 0; k < 3; k++) {
            float tone = base[k] * (0.84f + 0.16f * pool);
            tone *= 1.f - P.shadowAmt * sh;
            tone += (avgCol[k] - 0.5f) * 0.10f * pool * (0.4f + 0.6f * sh); // colour bleeding from the footage
            col[k] = tone;
        }
        if (P.floorReflect > 0.f) {
            V3 rd{d.x, -d.y, d.z};
            float Cr[3], Ar;
            traceVolume(p, rd, false, Cr, Ar);
            float k2 = P.floorReflect * (0.25f + 0.75f * std::pow(1.f - std::fabs(d.y), 3.f));
            for (int k = 0; k < 3; k++) col[k] = col[k] * (1.f - k2 * Ar) + k2 * Cr[k];
        }
        float f = sstep(4.f * rad, 16.f * rad, t); // fade to the studio horizon
        for (int k = 0; k < 3; k++) out[k] = col[k] * (1.f - f) + base[k] * f;
    }

    // ------------------------------------------------------------ anti-aliasing helper
    // true when the pixel differs noticeably from a neighbour (silhouette, pane edge, particle...)
    bool edgeAt(const std::vector<float>& v, int x, int y) const {
        const float* c = &v[((size_t)y * outW + x) * 4];
        const int nx[4] = {-1, 1, 0, 0}, ny[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; k++) {
            int qx = x + nx[k], qy = y + ny[k];
            if (qx < 0 || qy < 0 || qx >= outW || qy >= outH) continue;
            const float* n = &v[((size_t)qy * outW + qx) * 4];
            float dA = std::fabs(c[3] - n[3]);
            float dC = std::max(std::fabs(c[0] - n[0]), std::max(std::fabs(c[1] - n[1]), std::fabs(c[2] - n[2])));
            if (std::max(dA, 0.5f * dC) > 0.05f) return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------- finishing (full resolution)
    inline void bilBuf(const std::vector<float>& s, int w, int h, float X, float Y, float o[4]) {
        float fx = X - 0.5f, fy = Y - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        float tx = fx - x0, ty = fy - y0;
        auto P4 = [&](int x, int y) { x = std::min(std::max(x, 0), w - 1); y = std::min(std::max(y, 0), h - 1); return &s[((size_t)y * w + x) * 4]; };
        const float *a = P4(x0, y0), *b = P4(x0 + 1, y0), *c = P4(x0, y0 + 1), *d = P4(x0 + 1, y0 + 1);
        for (int k = 0; k < 4; k++) o[k] = (a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty;
    }

    inline void postProcess(const Params& P, std::vector<float>& img, int outW, int outH, float rs, int seed) {
        const size_t n = (size_t)outW * outH;
        if (P.glowAmt > 0.f) {
            std::vector<float> g(n * 4, 0.f), t(n * 4, 0.f);
            for (size_t i = 0; i < n; i++)
                for (int c = 0; c < 3; c++) g[i * 4 + c] = std::max(0.f, img[i * 4 + c] - P.glowThresh);
            int r = std::max(1, (int)std::lround(P.glowRad * rs));
            for (int it = 0; it < 2; it++) { blurAxis(g, t, outW, outH, r, true); blurAxis(t, g, outW, outH, r, false); }
            for (size_t i = 0; i < n; i++) {
                float mx = std::max(g[i * 4], std::max(g[i * 4 + 1], g[i * 4 + 2])) * P.glowAmt;
                for (int c = 0; c < 3; c++) img[i * 4 + c] += g[i * 4 + c] * P.glowAmt;
                img[i * 4 + 3] = std::min(1.f, img[i * 4 + 3] + mx);
            }
        }
        if (P.caAmt > 0.f) {
            std::vector<float> src = img;
            float cx = outW * 0.5f, cy = outH * 0.5f, inv = 1.f / std::sqrt(cx * cx + cy * cy);
            parallelRows(0, outH, [&](int a, int b) {
                for (int y = a; y < b; y++)
                    for (int x = 0; x < outW; x++) {
                        float ox = (x + 0.5f - cx) * inv * P.caAmt * rs, oy = (y + 0.5f - cy) * inv * P.caAmt * rs;
                        float r[4], g[4], bl[4];
                        bilBuf(src, outW, outH, x + 0.5f + ox, y + 0.5f + oy, r);
                        bilBuf(src, outW, outH, x + 0.5f, y + 0.5f, g);
                        bilBuf(src, outW, outH, x + 0.5f - ox, y + 0.5f - oy, bl);
                        float* o = &img[((size_t)y * outW + x) * 4];
                        o[0] = r[0]; o[1] = g[1]; o[2] = bl[2];
                        o[3] = std::max(r[3], std::max(g[3], bl[3]));
                    }
            });
        }
        if (P.rolloff > 0.f) { // highlight roll-off: overflow spills to white, then a soft shoulder
            const float k = clamp01(P.rolloff), knee = 0.8f;
            parallelRows(0, outH, [&](int a, int b) {
                for (int y = a; y < b; y++)
                    for (int x = 0; x < outW; x++) {
                        float* o = &img[((size_t)y * outW + x) * 4];
                        float Y = 0.2126f * o[0] + 0.7152f * o[1] + 0.0722f * o[2];
                        if (Y > 1.f) {
                            float t = clamp01((Y - 1.f) * 0.6f) * k;
                            for (int c = 0; c < 3; c++) o[c] = o[c] * (1.f - t) + Y * t;
                        }
                        for (int c = 0; c < 3; c++) {
                            float v = o[c];
                            if (v > knee) v = knee + (1.f - knee) * (1.f - std::exp(-(v - knee) / (1.f - knee)));
                            o[c] = o[c] * (1.f - k) + v * k;
                        }
                        o[3] = std::max(o[3], std::min(1.f, std::max(o[0], std::max(o[1], o[2]))));
                    }
            });
        }
        if (P.grain > 0.f) {
            parallelRows(0, outH, [&](int a, int b) {
                for (int y = a; y < b; y++)
                    for (int x = 0; x < outW; x++) {
                        float* o = &img[((size_t)y * outW + x) * 4];
                        float nz = (rand01(x, y, seed + 7919) - 0.5f) * 2.f * P.grain * o[3];
                        o[0] += nz; o[1] += nz; o[2] += nz;
                    }
            });
        }
    }

// Snap to Front: cross-fade onto the flat source frame (done at full resolution so it stays sharp).
inline void snapBlend(const Params& P, const std::vector<FrameView>& frames, int w, int h, std::vector<float>& img) {
    if (P.snap <= 0.f || frames.empty()) return;
    const int N = (int)frames.size();
    int cj = std::min(std::max(P.flip ? P.before : P.after, 0), N - 1);
    float sn = clamp01(P.snap);
    parallelRows(0, h, [&](int a, int b) {
        for (int y = a; y < b; y++)
            for (int x = 0; x < w; x++) {
                float c[4];
                frames[cj].bilinear((x + 0.5f) / w, (y + 0.5f) / h, c);
                float* o = &img[((size_t)y * w + x) * 4];
                for (int k = 0; k < 3; k++) o[k] = o[k] * (1.f - sn) + c[k] * c[3] * sn;
                o[3] = o[3] * (1.f - sn) + c[3] * sn;
            }
    });
}

inline void upscaleBilinear(const std::vector<float>& src, int sw, int sh, std::vector<float>& dst, int dw, int dh) {
    dst.assign((size_t)dw * dh * 4, 0.f);
    parallelRows(0, dh, [&](int a, int b) {
        for (int y = a; y < b; y++)
            for (int x = 0; x < dw; x++) {
                float o[4];
                bilBuf(src, sw, sh, (x + 0.5f) * sw / (float)dw, (y + 0.5f) * sh / (float)dh, o);
                float* d = &dst[((size_t)y * dw + x) * 4];
                d[0] = o[0]; d[1] = o[1]; d[2] = o[2]; d[3] = o[3];
            }
    });
}

// Main entry point. scale < 1 renders the 3D part at a lower resolution (Draft) and upsamples it;
// snap blend and finishing (glow, aberration, grain, roll-off) always run at full resolution.
inline void renderFrameScaled(const Params& P, const std::vector<FrameView>& frames, int fullW, int fullH, float scale,
                              float renderScale, uint32_t seed, std::vector<float>& out) {
    if (scale >= 0.99f) {
        Renderer r(P, frames, fullW, fullH, renderScale, seed);
        r.run(out);
    } else {
        int sw = std::max(16, (int)std::lround(fullW * scale)), sh = std::max(16, (int)std::lround(fullH * scale));
        std::vector<float> small;
        Renderer r(P, frames, sw, sh, renderScale * scale, seed);
        r.run(small);
        upscaleBilinear(small, sw, sh, out, fullW, fullH);
    }
    snapBlend(P, frames, fullW, fullH, out);
    postProcess(P, out, fullW, fullH, renderScale, (int)seed);
}

inline void renderFrame(const Params& P, const std::vector<FrameView>& frames, int outW, int outH,
                        float renderScale, uint32_t seed, std::vector<float>& out) {
    renderFrameScaled(P, frames, outW, outH, 1.f, renderScale, seed, out);
}

} // namespace chrono
