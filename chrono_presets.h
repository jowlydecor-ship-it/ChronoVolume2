// ChronoVolume - preset table (shared by the plugin and the preview tool). No OpenFX dependency.
#pragma once
#include "chrono_core.h"

namespace chrono {

struct Preset {
    const char* name;
    int mode, style, before, after, opacity, colorMode; // mode: 0 solid 1 translucent 2 slices 3 cross | style: 0 volume 1 dust 2 lines
    float depth, bend, yaw, pitch, density, fade, glow, glowRad, slopeX, ca, sliceOpacity, dotDensity, edge, frost;
    bool motion, studio;
};

inline const Preset kPresets[] = {
    // name               mode sty bef aft op col  depth bend yaw pit dens fade glow gRad slX  ca  slOp dotD  edge frost motion studio
    {"Default",             1, 0, 24, 0, 1, 0,  1.5f, 0.f, 25, 15,  6.f, .3f, 0.f, 12.f, 1.f, 0.f, .9f,  .35f, 0.f, 0.f, false, false},
    {"Ghost Trail",         1, 0, 16, 0, 0, 0,  1.2f, 0.f, 20, 20,  4.f, .7f, .3f, 12.f, 1.f, 0.f, .9f,  .35f, 0.f, 0.f, true,  false},
    {"Motion Ghosts",       1, 0, 24, 0, 0, 0,  1.5f, 0.f, 20, 20,  5.f, .5f, .6f, 16.f, 1.f, 0.f, .9f,  .35f, 0.f, 0.f, true,  false},
    {"Solid Block",         0, 0, 30, 0, 0, 0,  1.5f, 0.f, 30, 20,  6.f, 0.f, 0.f, 12.f, 1.f, 0.f, .9f,  .35f, .3f, 0.f, false, false},
    {"Time Ring",           0, 0, 48, 0, 0, 0,  2.5f, 1.f,  0, 55,  6.f, 0.f, 0.f, 12.f, 1.f, 0.f, .9f,  .35f, .3f, 0.f, false, false},
    {"Time Ring (Faded)",   1, 0, 48, 0, 0, 0,  2.5f, 1.f,  0, 50,  8.f, .8f, .4f, 16.f, 1.f, 0.f, .9f,  .35f, 0.f, 0.f, true,  false},
    {"Motion Sculpture",    0, 0, 40, 0, 0, 0,  2.0f, 0.f, 35, 15,  6.f, 0.f, .2f, 12.f, 1.f, 0.f, .9f,  .35f, 0.f, 0.f, true,  false},
    {"Slit Scan",           3, 0, 30,30, 0, 0,  1.5f, 0.f, 25, 10,  6.f, 0.f, 0.f, 12.f, 1.f, 0.f, .9f,  .35f, 0.f, 0.f, false, false},
    {"Frame Stack",         2, 0, 12, 0, 0, 0,  2.2f, 0.f, 40,  8,  6.f, 0.f, 0.f, 12.f, 1.f, 0.f, .95f, .35f, .5f, 0.f, false, false},
    {"Lidar Scan",          2, 2, 24, 0, 1, 0,  1.8f, 0.f, 30, 20,  6.f, .2f, .5f, 14.f, 1.f, 0.f, .9f,  .35f, 0.f, 0.f, false, false},
    {"Data Cloud",          1, 1, 30, 0, 0, 0,  1.5f, 0.f, 30, 15, 12.f, 0.f, .3f, 12.f, 1.f, 0.f, .9f,  .6f,  0.f, 0.f, true,  false},
    {"Neon Dust",           1, 1, 30, 0, 0, 1,  1.5f, 0.f, 30, 15, 14.f, .2f, 1.f, 18.f, 1.f, 0.f, .9f,  .6f,  0.f, 0.f, true,  false},
    {"Hologram",            1, 2, 20, 0, 1, 1,  1.6f, 0.f, 20, 10,  8.f, .3f, .8f, 16.f, 1.f, 6.f, .9f,  .35f, 0.f, 0.f, true,  false},
    {"Dreamy Glow",         1, 0, 30, 0, 1, 0,  1.5f, 0.f, 25, 15,  5.f, .5f,1.5f, 40.f, 1.f, 3.f, .9f,  .35f, 0.f, 0.f, false, false},
    {"Glass Block",         2, 0, 30, 0, 0, 0,  1.8f, 0.f, 30, 20,  6.f, 0.f, 0.f, 14.f, 1.f, 2.f, .22f, .35f, 1.0f, 0.f, false, true},
    {"Frosted Panes",       2, 0, 16, 0, 0, 0,  2.2f, 0.f, 35, 12,  6.f, 0.f, 0.f, 25.f, 1.f, 0.f, .55f, .35f, .4f, .9f, false, true},
    {"Studio Sculpture",    0, 0, 40, 0, 0, 0,  2.0f, 0.f, 35, 15,  6.f, 0.f, 0.f, 12.f, 1.f, 0.f, .9f,  .35f, 0.f, 0.f, true,  true},
};
inline constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

// Zoom that keeps the whole cube visible (the cross section is flat, so it fills the frame).
inline float presetZoom(const Preset& p) {
    if (p.mode == 3) return 1.f;
    if (p.bend > 0.5f) return 0.62f;
    return p.depth > 1.9f ? 0.62f : 0.74f;
}

// Applies the preset's values to a Params struct (used by the preview tool).
inline void applyPreset(Params& P, const Preset& p) {
    P.mode = p.mode; P.style = p.style; P.before = p.before; P.after = p.after;
    P.opacitySrc = p.opacity; P.colorMode = p.colorMode;
    P.depth = p.depth; P.bend = p.bend; P.yaw = p.yaw; P.pitch = p.pitch;
    P.density = p.density; P.trailFade = p.fade; P.glowAmt = p.glow; P.glowRad = p.glowRad;
    P.crossSlopeX = p.slopeX; P.caAmt = p.ca; P.sliceOpacity = p.sliceOpacity; P.dotDensity = p.dotDensity;
    P.edgeGlow = p.edge; P.frost = p.frost;
    P.motionOnly = p.motion;
    P.floorOn = p.studio;
    P.zoom = presetZoom(p);
    P.bg[0] = P.bg[1] = P.bg[2] = P.bg[3] = 0.f;
}

} // namespace chrono
