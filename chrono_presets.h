// ChronoVolume - preset table (shared by the plugin and the preview tool). No OpenFX dependency.
#pragma once
#include "chrono_core.h"

namespace chrono {

struct Preset {
    const char* name;
    int mode, style, before, after, opacity, colorMode; // mode: 0 solid 1 translucent 2 slices 3 cross | style: 0 volume 1 dust 2 lines
    float depth, bend, yaw, pitch, density, emit, fade, glow, glowRad, slopeX, ca, sliceOpacity;
    int dotGrid;
    float dotDensity, edge, frost;
    bool motion, studio;
};

inline const Preset kPresets[] = {
    {"Default", 1, 0, 24, 0, 1, 0, 1.5f, 0.0f, 25.0f, 15.0f, 3.0f, 1.2f, 0.3f, 0.25f, 14.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, false, false},
    {"Ghost Trail", 1, 0, 16, 0, 0, 0, 1.2f, 0.0f, 20.0f, 20.0f, 2.0f, 3.0f, 0.7f, 0.6f, 14.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, true, false},
    {"Motion Ghosts", 1, 0, 24, 0, 0, 0, 1.5f, 0.0f, 20.0f, 20.0f, 2.5f, 3.5f, 0.5f, 0.8f, 16.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, true, false},
    {"Solid Block", 0, 0, 30, 0, 0, 0, 1.5f, 0.0f, 30.0f, 20.0f, 6.0f, 0.0f, 0.0f, 0.1f, 12.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.3f, 0.0f, false, false},
    {"Time Ring", 0, 0, 48, 0, 0, 0, 2.5f, 1.0f, 0.0f, 55.0f, 6.0f, 0.0f, 0.0f, 0.1f, 12.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.3f, 0.0f, false, false},
    {"Time Ring (Faded)", 1, 0, 48, 0, 0, 0, 2.5f, 1.0f, 0.0f, 50.0f, 3.0f, 3.0f, 0.8f, 0.7f, 16.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, true, false},
    {"Motion Sculpture", 0, 0, 40, 0, 0, 0, 2.0f, 0.0f, 35.0f, 15.0f, 6.0f, 0.0f, 0.0f, 0.3f, 12.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.4f, 0.0f, true, false},
    {"Slit Scan", 3, 0, 30, 30, 0, 0, 1.5f, 0.0f, 25.0f, 10.0f, 6.0f, 0.0f, 0.0f, 0.1f, 12.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, false, false},
    {"Frame Stack", 2, 0, 12, 0, 0, 0, 2.2f, 0.0f, 40.0f, 8.0f, 6.0f, 0.0f, 0.0f, 0.1f, 12.0f, 1.0f, 0.0f, 0.95f, 60, 0.35f, 0.5f, 0.0f, false, false},
    {"Lidar Scan", 2, 2, 24, 0, 1, 0, 1.8f, 0.0f, 30.0f, 20.0f, 6.0f, 2.0f, 0.2f, 0.8f, 14.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, false, false},
    {"Data Cloud", 1, 1, 30, 0, 0, 0, 1.5f, 0.0f, 30.0f, 15.0f, 6.0f, 3.0f, 0.0f, 0.6f, 12.0f, 1.0f, 0.0f, 0.9f, 110, 0.6f, 0.0f, 0.0f, true, false},
    {"Neon Dust", 1, 1, 30, 0, 0, 1, 1.5f, 0.0f, 30.0f, 15.0f, 6.0f, 4.0f, 0.2f, 1.2f, 18.0f, 1.0f, 0.0f, 0.9f, 110, 0.7f, 0.0f, 0.0f, true, false},
    {"Hologram", 1, 2, 20, 0, 1, 1, 1.6f, 0.0f, 20.0f, 10.0f, 6.0f, 2.5f, 0.3f, 1.0f, 16.0f, 1.0f, 6.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, true, false},
    {"Dreamy Glow", 1, 0, 30, 0, 1, 0, 1.5f, 0.0f, 25.0f, 15.0f, 2.0f, 2.5f, 0.5f, 1.5f, 40.0f, 1.0f, 3.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, false, false},
    {"Glass Block", 2, 0, 30, 0, 0, 0, 1.8f, 0.0f, 30.0f, 20.0f, 6.0f, 0.0f, 0.0f, 0.0f, 14.0f, 1.0f, 2.0f, 0.22f, 60, 0.35f, 1.0f, 0.0f, false, true},
    {"Frosted Panes", 2, 0, 16, 0, 0, 0, 2.2f, 0.0f, 35.0f, 12.0f, 6.0f, 0.0f, 0.0f, 0.0f, 25.0f, 1.0f, 0.0f, 0.55f, 60, 0.35f, 0.4f, 0.9f, false, true},
    {"Studio Sculpture", 0, 0, 40, 0, 0, 0, 2.0f, 0.0f, 35.0f, 15.0f, 6.0f, 0.0f, 0.0f, 0.0f, 12.0f, 1.0f, 0.0f, 0.9f, 60, 0.35f, 0.0f, 0.0f, true, true},
};
inline constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

// Zoom that keeps the whole cube visible (the cross section is flat, so it fills the frame).
inline float presetZoom(const Preset& p) {
    if (p.mode == 3) return 1.f;
    if (p.bend > 0.5f) return 0.62f;
    return p.depth > 1.9f ? 0.62f : 0.74f;
}

// Applies the preset's look to a Params struct (camera angles are absolute here).
inline void applyPreset(Params& P, const Preset& p) {
    P.mode = p.mode; P.style = p.style; P.before = p.before; P.after = p.after;
    P.opacitySrc = p.opacity; P.colorMode = p.colorMode;
    P.depth = p.depth; P.bend = p.bend; P.yaw = p.yaw; P.pitch = p.pitch;
    P.density = p.density; P.emission = p.emit; P.trailFade = p.fade; P.glowAmt = p.glow; P.glowRad = p.glowRad;
    P.crossSlopeX = p.slopeX; P.caAmt = p.ca; P.sliceOpacity = p.sliceOpacity;
    P.dotGrid = p.dotGrid; P.dotDensity = p.dotDensity;
    P.edgeGlow = p.edge; P.frost = p.frost;
    P.motionOnly = p.motion;
    P.floorOn = p.studio;
    P.zoom = presetZoom(p);
    P.bg[0] = P.bg[1] = P.bg[2] = P.bg[3] = 0.f;
}

} // namespace chrono
