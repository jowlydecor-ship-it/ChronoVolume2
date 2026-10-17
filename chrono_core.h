// ChronoVolume - rendering core (no OpenFX dependency).
// Original implementation: stacks N frames of a clip into a 3D "time volume"
// and renders it (CPU, multithreaded) with a virtual camera.
#pragma once

#include <algorithm>
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
    bool flip = false;       // flip time direction (front = oldest instead of newest)
    bool loop = false;       // handled by the host layer (time wrapping)
    bool blend = true;       // blend neighbouring frames (cheap smoothing, NOT optical flow)

    float depth = 1.5f;      // depth of the time block, in image heights
    float density = 6.f;     // translucent density
    float sliceOpacity = 0.9f;
    float trailFade = 0.3f;  // fades older frames (0..1)

    float yaw = 25.f, pitch = 15.f, roll = 0.f, fov = 35.f, zoom = 1.f;
    float posX = 0.f, posY = 0.f;     // move the cube on screen (fractions of frame width/height)
    float scaleW = 1.f, scaleH = 1.f; // cube width / height proportions
    float bend = 0.f;        // 0 = straight stack, 1 = closed ring
    int steps = 96;          // ray-march quality

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

    int colorMode = 0;       // 0 source colours, 1 tint by age
    float colorNew[3] = {1.f, 0.6f, 0.2f};
    float colorOld[3] = {0.2f, 0.5f, 1.f};
    float colorMix = 0.6f;

    float crossPos = 0.5f, crossSlopeX = 1.f, crossSlopeY = 0.f;

    float glowAmt = 0.f, glowRad = 12.f, glowThresh = 0.6f;
    float caAmt = 0.f;       // chromatic aberration, pixels at the image corner
    float grain = 0.f;

    float bg[4] = {0.f, 0.f, 0.f, 0.f}; // premultiplied background colour
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

// Simple row-parallel helper (the OFX multithread suite could be used instead).
inline void parallelRows(int y0, int y1, const std::function<void(int, int)>& fn) {
    if (y1 <= y0) return;
    unsigned n = std::max(1u, std::thread::hardware_concurrency());
    n = std::min<unsigned>(n, (unsigned)(y1 - y0));
    int per = (y1 - y0 + (int)n - 1) / (int)n;
    std::vector<std::thread> th;
    for (unsigned i = 0; i < n; i++) {
        int a = y0 + (int)i * per, b = std::min(y1, a + per);
        if (a >= b) break;
        th.emplace_back(fn, a, b);
    }
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

    // out: outW*outH*4 floats, premultiplied RGBA, row 0 = bottom.
    void run(std::vector<float>& out) {
        out.assign((size_t)outW * outH * 4, 0.f);
        prepare();
        parallelRows(0, outH, [&](int a, int b) {
            for (int y = a; y < b; y++)
                for (int x = 0; x < outW; x++) shade(x, y, &out[((size_t)y * outW + x) * 4]);
        });
        post(out);
    }

private:
    const Params& P;
    const std::vector<FrameView>& frames;
    int N, outW, outH;
    float rs;
    int seed;

    // scene
    float W = 1.78f, Wc = 1.78f, Hc = 1.f, D = 1.5f, Rc = 1.f, theta = 0.f, tanV = 0.3f, tanH = 0.5f, rad = 1.f;
    bool bent = false;
    V3 cam{0, 0, -3}, fwd{0, 0, 1}, right{1, 0, 0}, up{0, 1, 0}, T{0, 0, 0};
    std::vector<float> bg; // temporal median background (RGB) for Motion Only
    int bgW = 0, bgH = 0;

    void prepare() {
        int fw = 0, fh = 0;
        for (auto& f : frames) if (f.w > 0) { fw = f.w; fh = f.h; break; }
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
            R = (0.5f * Hc / tanV + D * 0.5f) / zoom;
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

        if (P.motionOnly && P.mode != -1 && fw > 0) {
            bgW = fw; bgH = fh;
            bg.assign((size_t)bgW * bgH * 3, 0.f);
            parallelRows(0, bgH, [&](int a, int b) {
                std::vector<float> tmp(std::max(1, N));
                for (int y = a; y < b; y++)
                    for (int x = 0; x < bgW; x++)
                        for (int c = 0; c < 3; c++) {
                            for (int j = 0; j < N; j++) tmp[j] = frames[j].px(x, y)[c];
                            std::nth_element(tmp.begin(), tmp.begin() + N / 2, tmp.begin() + N);
                            bg[((size_t)y * bgW + x) * 3 + c] = tmp[N / 2];
                        }
            });
        }
    }

    // ---- sampling
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

    float opacityOf(const float c[4], float u, float v) const {
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
                float d = std::sqrt(dr * dr + dg * dg + db * db);
                m = sstep(0.f, std::max(0.01f, P.keyTol), d);
                break;
            }
            case 5: m = c[3]; break;
            default: m = 1.f;
        }
        m = clamp01(m * P.gain);
        if (P.invert) m = 1.f - m;
        if (P.motionOnly && bgW > 0) {
            int bx = std::min(std::max((int)(u * bgW), 0), bgW - 1);
            int by = std::min(std::max((int)(v * bgH), 0), bgH - 1);
            const float* b = &bg[((size_t)by * bgW + bx) * 3];
            float d = std::max(std::fabs(c[0] - b[0]), std::max(std::fabs(c[1] - b[1]), std::fabs(c[2] - b[2])));
            m *= sstep(P.motionThresh, P.motionThresh + P.motionSoft + 1e-4f, d);
        }
        return m;
    }

    // colour + opacity of the volume at (u,v,j); false = empty.
    bool evalVolume(float u, float v, float j, float rgb[3], float& m) const {
        float c[4];
        if (P.style == 1) { // voxel dust
            float Gv = (float)std::max(4, P.dotGrid), Gu = Gv * Wc, Gv2 = Gv * Hc;
            float su = u * Gu, sv = v * Gv2;
            int iu = (int)std::floor(su), iv = (int)std::floor(sv), jc = (int)std::lround(j);
            if (rand01(iu, iv, jc) > P.dotDensity) return false;
            float du = su - iu - 0.5f - (rand01(iu, iv, jc * 3 + 1) - 0.5f) * 0.5f;
            float dv = sv - iv - 0.5f - (rand01(iu, iv, jc * 3 + 2) - 0.5f) * 0.5f;
            float dw = (j - jc) * (D / (float)std::max(1, N - 1)) * Gv;
            float r = P.dotSize * 0.5f;
            if (du * du + dv * dv + dw * dw > r * r) return false;
            float uc = (iu + 0.5f) / Gu, vc = (iv + 0.5f) / Gv2;
            sampleFrames((float)jc, uc, vc, c);
            m = opacityOf(c, uc, vc);
        } else {
            if (P.style == 2) { // scan lines
                float f = v * (float)std::max(1, P.lines);
                f -= std::floor(f);
                if (f > P.lineWidth) return false;
            }
            sampleFrames(j, u, v, c);
            m = opacityOf(c, u, v);
        }
        if (m <= 0.f) return false;
        rgb[0] = c[0]; rgb[1] = c[1]; rgb[2] = c[2];
        return true;
    }

    bool toVolume(const V3& p, float& u, float& v, float& w) const {
        v = p.y / Hc + 0.5f;
        if (v < 0.f || v > 1.f) return false;
        if (!bent) {
            u = p.x / Wc + 0.5f;
            w = p.z / D + 0.5f;
        } else {
            float rx = p.x + Rc, rz = p.z;
            float r = std::sqrt(rx * rx + rz * rz);
            u = (r - Rc) / Wc + 0.5f;
            w = std::atan2(rz, rx) / theta + 0.5f;
        }
        return u >= 0.f && u <= 1.f && w >= -1e-4f && w <= 1.f + 1e-4f;
    }

    bool hitBox(const V3& o, const V3& d, float& t0, float& t1) const {
        t0 = 0.f; t1 = 1e30f;
        const float bmin[3] = {-Wc * 0.5f, -Hc * 0.5f, -D * 0.5f}, bmax[3] = {Wc * 0.5f, Hc * 0.5f, D * 0.5f};
        const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
        for (int i = 0; i < 3; i++) {
            if (std::fabs(dd[i]) < 1e-8f) { if (oo[i] < bmin[i] || oo[i] > bmax[i]) return false; continue; }
            float a = (bmin[i] - oo[i]) / dd[i], b = (bmax[i] - oo[i]) / dd[i];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a); t1 = std::min(t1, b);
            if (t0 > t1) return false;
        }
        return true;
    }

    void finish(float C[3], float A, float out[4]) const {
        out[0] = C[0] + (1.f - A) * P.bg[0];
        out[1] = C[1] + (1.f - A) * P.bg[1];
        out[2] = C[2] + (1.f - A) * P.bg[2];
        out[3] = A + (1.f - A) * P.bg[3];
    }

    void tint(float rgb[3], float w) const {
        if (P.colorMode != 1) return;
        for (int k = 0; k < 3; k++) {
            float t = P.colorNew[k] * (1.f - w) + P.colorOld[k] * w;
            rgb[k] = rgb[k] * (1.f - P.colorMix) + t * P.colorMix;
        }
    }

    // ---- per-pixel shading
    void shade(int x, int y, float out[4]) const {
        float C[3] = {0, 0, 0};
        if (P.mode == 3) { // cross section: a slanted plane through the time stack
            float u = (x + 0.5f) / outW, v = (y + 0.5f) / outH;
            float j = (P.crossPos + P.crossSlopeX * (u - 0.5f) + P.crossSlopeY * (v - 0.5f)) * (float)(N - 1);
            j = std::min(std::max(j, 0.f), (float)(N - 1));
            float c[4];
            sampleFrames(j, u, v, c);
            float a = P.motionOnly ? opacityOf(c, u, v) : 1.f;
            C[0] = c[0] * a; C[1] = c[1] * a; C[2] = c[2] * a;
            finish(C, a, out);
            return;
        }

        float nx = ((x + 0.5f) / outW) * 2.f - 1.f - 2.f * P.posX, ny = ((y + 0.5f) / outH) * 2.f - 1.f - 2.f * P.posY;
        V3 d = norm(fwd + right * (nx * tanH) + up * (ny * tanV));
        float A = 0.f;

        if (P.mode == 2) { // separated slices: analytic ray / card intersections
            struct Hit { float t; int j; } hits[kMaxFrames];
            int nh = 0;
            for (int j = 0; j < N; j++) {
                float w = N > 1 ? (float)j / (float)(N - 1) : 0.5f, t;
                if (!bent) {
                    if (std::fabs(d.z) < 1e-6f) continue;
                    t = ((w - 0.5f) * D - cam.z) / d.z;
                } else {
                    float phi = (w - 0.5f) * theta;
                    V3 n{-std::sin(phi), 0, std::cos(phi)};
                    float den = dot(d, n);
                    if (std::fabs(den) < 1e-6f) continue;
                    t = dot(V3{-Rc, 0, 0} - cam, n) / den;
                }
                if (t <= 0.f) continue;
                float u, v, w2;
                if (!toVolume(cam + d * t, u, v, w2)) continue;
                if (bent && std::fabs(w2 - w) > 1e-3f) continue;
                int k = nh++;
                while (k > 0 && hits[k - 1].t > t) { hits[k] = hits[k - 1]; k--; }
                hits[k] = {t, j};
            }
            for (int h = 0; h < nh && A < 0.995f; h++) {
                float u = 0.f, v = 0.f, w2 = 0.f;
                toVolume(cam + d * hits[h].t, u, v, w2);
                float rgb[3], m;
                float jf = (float)hits[h].j, w = N > 1 ? jf / (float)(N - 1) : 0.f;
                if (!evalVolume(u, v, jf, rgb, m)) continue;
                m *= 1.f - P.trailFade * w;
                float a = clamp01(m * P.sliceOpacity);
                tint(rgb, w);
                for (int k = 0; k < 3; k++) C[k] += (1.f - A) * a * rgb[k];
                A += (1.f - A) * a;
            }
            finish(C, A, out);
            return;
        }

        float t0, t1;
        bool hit;
        if (!bent) hit = hitBox(cam, d, t0, t1);
        else {
            V3 oc = cam - T;
            float b = dot(oc, d), c = dot(oc, oc) - rad * rad, disc = b * b - c;
            hit = disc >= 0.f;
            if (hit) { float s = std::sqrt(disc); t0 = std::max(0.f, -b - s); t1 = -b + s; hit = t1 > t0; }
        }
        if (!hit) { finish(C, 0.f, out); return; }

        const int steps = std::max(8, P.steps);
        const float dt = (t1 - t0) / steps;
        float t = t0 + dt * rand01(x, y, seed);
        for (int i = 0; i < steps && A < 0.995f; i++, t += dt) {
            float u, v, w;
            if (!toVolume(cam + d * t, u, v, w)) continue;
            float rgb[3], m;
            if (!evalVolume(u, v, w * (float)(N - 1), rgb, m)) continue;
            m *= 1.f - P.trailFade * w;
            float a = (P.mode == 0) ? sstep(0.3f, 0.7f, m) : 1.f - std::exp(-P.density * m * dt);
            tint(rgb, w);
            for (int k = 0; k < 3; k++) C[k] += (1.f - A) * a * rgb[k];
            A += (1.f - A) * a;
        }
        finish(C, A, out);
    }

    // ---- finishing: glow, chromatic aberration, grain
    static void bilBuf(const std::vector<float>& s, int w, int h, float X, float Y, float o[4]) {
        float fx = X - 0.5f, fy = Y - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        float tx = fx - x0, ty = fy - y0;
        auto P4 = [&](int x, int y) { x = std::min(std::max(x, 0), w - 1); y = std::min(std::max(y, 0), h - 1); return &s[((size_t)y * w + x) * 4]; };
        const float *a = P4(x0, y0), *b = P4(x0 + 1, y0), *c = P4(x0, y0 + 1), *d = P4(x0 + 1, y0 + 1);
        for (int k = 0; k < 4; k++) o[k] = (a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty;
    }

    void post(std::vector<float>& img) const {
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
};

// Convenience entry point.
inline void renderFrame(const Params& P, const std::vector<FrameView>& frames, int outW, int outH,
                        float renderScale, uint32_t seed, std::vector<float>& out) {
    Renderer r(P, frames, outW, outH, renderScale, seed);
    r.run(out);
}

} // namespace chrono
