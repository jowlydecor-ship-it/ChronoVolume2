// ChronoVolume - OpenFX plugin (original implementation, not derived from any commercial plugin).
// Built on the OpenFX C++ Support library. Rendering lives in chrono_core.h.
//
// NOTE: written against the OpenFX 1.4 Support library (ofxsImageEffect.h). If your SDK
// version renamed something (e.g. FramesNeededSetter), adjust the few OFX calls below.

#include "ofxsImageEffect.h"

#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "chrono_presets.h"

using namespace OFX;

#define kPluginName "ChronoVolume"
#define kPluginGrouping "Time"
#define kPluginIdentifier "com.example.ChronoVolume"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

namespace {

// Presets live in chrono_presets.h (shared with the preview tool).
using chrono::kNumPresets;
using chrono::kPresets;

// ---------------------------------------------------------------- instance
class ChronoVolumePlugin : public ImageEffect {
public:
    explicit ChronoVolumePlugin(OfxImageEffectHandle handle) : ImageEffect(handle) {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);

        preset_ = fetchChoiceParam("preset");
        mode_ = fetchChoiceParam("mode");
        style_ = fetchChoiceParam("style");
        opacitySrc_ = fetchChoiceParam("opacitySrc");
        colorMode_ = fetchChoiceParam("colorMode");
        motionMethod_ = fetchChoiceParam("motionMethod");
        orbit_ = fetchDouble2DParam("orbit");

        before_ = fetchIntParam("framesBefore");
        after_ = fetchIntParam("framesAfter");
        steps_ = fetchIntParam("steps");
        dotGrid_ = fetchIntParam("dotGrid");
        lines_ = fetchIntParam("lines");

        loop_ = fetchBooleanParam("loop");
        flip_ = fetchBooleanParam("flip");
        blend_ = fetchBooleanParam("blend");
        motionOnly_ = fetchBooleanParam("motionOnly");
        invert_ = fetchBooleanParam("invert");

        for (const char* n : {"depth", "density", "sliceOpacity", "trailFade", "yaw", "pitch", "fov", "zoom",
                              "bend", "snap", "shading", "edgeGlow", "frost", "floorReflect", "shadowAmt", "shadowSoft", "lightYaw", "lightPitch", "roll", "posX", "posY", "scaleW", "scaleH", "motionThresh", "motionSoft", "gain", "keyTol", "dotSize", "dotDensity",
                              "lineWidth", "colorMix", "crossPos", "crossSlopeX", "crossSlopeY", "glowAmt",
                              "glowRad", "glowThresh", "caAmt", "grain"})
            d_[n] = fetchDoubleParam(n);

        aa_ = fetchIntParam("aa");
        floorOn_ = fetchBooleanParam("floorOn");
        floorColor_ = fetchRGBParam("floorColor");
        keyColor_ = fetchRGBParam("keyColor");
        colorNew_ = fetchRGBParam("colorNew");
        colorMid_ = fetchRGBParam("colorMid");
        colorOld_ = fetchRGBParam("colorOld");
        bg_ = fetchRGBAParam("bgColor");
    }

    void render(const RenderArguments& args) override;
    void changedParam(const InstanceChangedArgs& args, const std::string& paramName) override;
    void getFramesNeeded(const FramesNeededArguments& args, FramesNeededSetter& frames) override;

private:
    Clip *dstClip_, *srcClip_;
    ChoiceParam *preset_, *mode_, *style_, *opacitySrc_, *colorMode_, *motionMethod_;
    Double2DParam* orbit_;
    IntParam *before_, *after_, *steps_, *dotGrid_, *lines_, *aa_;
    BooleanParam *loop_, *flip_, *blend_, *motionOnly_, *invert_, *floorOn_;
    std::map<std::string, DoubleParam*> d_;
    RGBParam *keyColor_, *colorNew_, *colorMid_, *colorOld_, *floorColor_;
    RGBAParam* bg_;

    // In this SDK version ChoiceParam only offers getValue(int&) / getValueAtTime(double, int&).
    static int choiceAt(ChoiceParam* p, double t) {
        int v = 0;
        p->getValueAtTime(t, v);
        return v;
    }

    double D(const char* n, double t) { return d_[n]->getValueAtTime(t); }

    chrono::Params readParams(double t) {
        chrono::Params P;
        P.mode = choiceAt(mode_, t);
        P.style = choiceAt(style_, t);
        P.opacitySrc = choiceAt(opacitySrc_, t);
        P.colorMode = choiceAt(colorMode_, t);
        P.motionMethod = choiceAt(motionMethod_, t);
        P.before = before_->getValueAtTime(t);
        P.after = after_->getValueAtTime(t);
        P.steps = steps_->getValueAtTime(t);
        P.dotGrid = dotGrid_->getValueAtTime(t);
        P.lines = lines_->getValueAtTime(t);
        P.loop = loop_->getValueAtTime(t);
        P.flip = flip_->getValueAtTime(t);
        P.blend = blend_->getValueAtTime(t);
        P.motionOnly = motionOnly_->getValueAtTime(t);
        P.invert = invert_->getValueAtTime(t);
        P.depth = (float)D("depth", t);
        P.density = (float)D("density", t);
        P.sliceOpacity = (float)D("sliceOpacity", t);
        P.trailFade = (float)D("trailFade", t);
        P.yaw = (float)D("yaw", t);
        P.pitch = (float)D("pitch", t);
        {   // on-screen handle: centre = no extra rotation; drag to orbit
            double ox = 0.5, oy = 0.5;
            orbit_->getValueAtTime(t, ox, oy);
            if (ox < -1.0 || ox > 2.0 || oy < -1.0 || oy > 2.0) { ox = 0.5; oy = 0.5; } // host gave pixels: ignore
            P.yaw += (float)((ox - 0.5) * 360.0);
            P.pitch += (float)((oy - 0.5) * 180.0);
        }
        P.fov = (float)D("fov", t);
        P.zoom = (float)D("zoom", t);
        P.bend = (float)D("bend", t);
        P.snap = (float)D("snap", t);
        P.aa = aa_->getValueAtTime(t);
        P.floorOn = floorOn_->getValueAtTime(t);
        P.shading = (float)D("shading", t);
        P.edgeGlow = (float)D("edgeGlow", t);
        P.frost = (float)D("frost", t);
        P.floorReflect = (float)D("floorReflect", t);
        P.shadowAmt = (float)D("shadowAmt", t);
        P.shadowSoft = (float)D("shadowSoft", t);
        P.lightYaw = (float)D("lightYaw", t);
        P.lightPitch = (float)D("lightPitch", t);
        P.roll = (float)D("roll", t);
        P.posX = (float)D("posX", t);
        P.posY = (float)D("posY", t);
        P.scaleW = (float)D("scaleW", t);
        P.scaleH = (float)D("scaleH", t);
        P.motionThresh = (float)D("motionThresh", t);
        P.motionSoft = (float)D("motionSoft", t);
        P.gain = (float)D("gain", t);
        P.keyTol = (float)D("keyTol", t);
        P.dotSize = (float)D("dotSize", t);
        P.dotDensity = (float)D("dotDensity", t);
        P.lineWidth = (float)D("lineWidth", t);
        P.colorMix = (float)D("colorMix", t);
        P.crossPos = (float)D("crossPos", t);
        P.crossSlopeX = (float)D("crossSlopeX", t);
        P.crossSlopeY = (float)D("crossSlopeY", t);
        P.glowAmt = (float)D("glowAmt", t);
        P.glowRad = (float)D("glowRad", t);
        P.glowThresh = (float)D("glowThresh", t);
        P.caAmt = (float)D("caAmt", t);
        P.grain = (float)D("grain", t);
        double r, g, b, a;
        keyColor_->getValueAtTime(t, r, g, b);
        P.keyColor[0] = (float)r; P.keyColor[1] = (float)g; P.keyColor[2] = (float)b;
        colorNew_->getValueAtTime(t, r, g, b);
        P.colorNew[0] = (float)r; P.colorNew[1] = (float)g; P.colorNew[2] = (float)b;
        floorColor_->getValueAtTime(t, r, g, b);
        P.floorColor[0] = (float)r; P.floorColor[1] = (float)g; P.floorColor[2] = (float)b;
        colorMid_->getValueAtTime(t, r, g, b);
        P.colorMid[0] = (float)r; P.colorMid[1] = (float)g; P.colorMid[2] = (float)b;
        colorOld_->getValueAtTime(t, r, g, b);
        P.colorOld[0] = (float)r; P.colorOld[1] = (float)g; P.colorOld[2] = (float)b;
        bg_->getValueAtTime(t, r, g, b, a);
        P.bg[0] = (float)(r * a); P.bg[1] = (float)(g * a); P.bg[2] = (float)(b * a); P.bg[3] = (float)a;
        return P;
    }

    static double mapTime(double tt, const OfxRangeD& r, bool loop) {
        if (loop) {
            double len = r.max - r.min + 1.0, x = std::fmod(tt - r.min, len);
            if (x < 0) x += len;
            return r.min + x;
        }
        return std::min(std::max(tt, r.min), r.max);
    }
};

void ChronoVolumePlugin::changedParam(const InstanceChangedArgs& args, const std::string& name) {
    if (name != "preset" || args.reason != eChangeUserEdit) return;
    int i = 0;
    preset_->getValue(i);
    if (i < 0 || i >= kNumPresets) return;
    const chrono::Preset& p = kPresets[i];
    beginEditBlock("ChronoVolume preset");
    mode_->setValue(p.mode);
    style_->setValue(p.style);
    opacitySrc_->setValue(p.opacity);
    colorMode_->setValue(p.colorMode);
    before_->setValue(p.before);
    after_->setValue(p.after);
    motionOnly_->setValue(p.motion);
    d_["depth"]->setValue(p.depth);
    d_["bend"]->setValue(p.bend);
    d_["yaw"]->setValue(p.yaw);
    d_["pitch"]->setValue(p.pitch);
    d_["density"]->setValue(p.density);
    d_["trailFade"]->setValue(p.fade);
    d_["glowAmt"]->setValue(p.glow);
    d_["glowRad"]->setValue(p.glowRad);
    d_["zoom"]->setValue(chrono::presetZoom(p));
    d_["posX"]->setValue(0.0);
    d_["posY"]->setValue(0.0);
    d_["roll"]->setValue(0.0);
    d_["crossSlopeX"]->setValue(p.slopeX);
    d_["caAmt"]->setValue(p.ca);
    d_["sliceOpacity"]->setValue(p.sliceOpacity);
    d_["dotDensity"]->setValue(p.dotDensity);
    d_["edgeGlow"]->setValue(p.edge);
    d_["frost"]->setValue(p.frost);
    floorOn_->setValue(p.studio);
    bg_->setValue(0.0, 0.0, 0.0, 0.0);
    endEditBlock();
}

void ChronoVolumePlugin::getFramesNeeded(const FramesNeededArguments& args, FramesNeededSetter& frames) {
    const double t = args.time;
    const int before = before_->getValueAtTime(t), after = after_->getValueAtTime(t);
    OfxRangeD cr = srcClip_->getFrameRange(), r;
    if (loop_->getValueAtTime(t)) {
        r = cr;
    } else {
        r.min = std::min(std::max(t - before, cr.min), cr.max);
        r.max = std::min(std::max(t + after, cr.min), cr.max);
    }
    frames.setFramesNeeded(*srcClip_, r);
}

void ChronoVolumePlugin::render(const RenderArguments& args) {
    const double t = args.time;
    chrono::Params P = readParams(t);
    P.before = std::min(std::max(P.before, 0), 63);
    P.after = std::min(std::max(P.after, 0), 63);
    const int N = P.before + P.after + 1;

    std::unique_ptr<Image> dst(dstClip_->fetchImage(t));
    if (!dst) throwSuiteStatusException(kOfxStatFailed);
    if (dst->getPixelDepth() != eBitDepthFloat || dst->getPixelComponents() != ePixelComponentRGBA)
        throwSuiteStatusException(kOfxStatErrFormat);

    OfxRangeD range = srcClip_->getFrameRange();
    std::vector<std::unique_ptr<Image>> imgs(N);
    std::vector<chrono::FrameView> views(N);
    for (int j = 0; j < N; j++) {
        // j = 0 is the front slice: newest frame (or oldest if flipped)
        double off = P.flip ? (double)(-P.before + j) : (double)(P.after - j);
        imgs[j].reset(srcClip_->fetchImage(mapTime(t + off, range, P.loop)));
        if (!imgs[j]) continue;
        OfxRectI b = imgs[j]->getBounds();
        views[j].base = static_cast<const char*>(imgs[j]->getPixelAddress(b.x1, b.y1));
        views[j].rowBytes = imgs[j]->getRowBytes();
        views[j].w = b.x2 - b.x1;
        views[j].h = b.y2 - b.y1;
    }

    // The effect does not support tiles, so the render window is the whole image.
    const OfxRectI db = dst->getBounds();
    const int W = db.x2 - db.x1, H = db.y2 - db.y1;
    std::vector<float> buf;
    chrono::renderFrame(P, views, W, H, (float)args.renderScale.x, (uint32_t)(int64_t)t, buf);

    const OfxRectI rw = args.renderWindow;
    for (int y = rw.y1; y < rw.y2; y++) {
        float* d = static_cast<float*>(dst->getPixelAddress(rw.x1, y));
        const float* s = &buf[((size_t)(y - db.y1) * W + (rw.x1 - db.x1)) * 4];
        std::copy(s, s + (size_t)(rw.x2 - rw.x1) * 4, d);
    }
}

// ---------------------------------------------------------------- factory
mDeclarePluginFactory(ChronoVolumeFactory, {}, {});

void ChronoVolumeFactory::describe(ImageEffectDescriptor& desc) {
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription("Stacks frames of a clip into a 3D time volume (solid, translucent, slices, cross "
                              "section), with ring bend, motion isolation, voxel dust, glow, aberration and grain.");
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(false);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}

void ChronoVolumeFactory::describeInContext(ImageEffectDescriptor& desc, ContextEnum) {
    ClipDescriptor* src = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    src->addSupportedComponent(ePixelComponentRGBA);
    src->setTemporalClipAccess(true);
    src->setSupportsTiles(false);
    src->setIsMask(false);

    ClipDescriptor* dst = desc.defineClip(kOfxImageEffectOutputClipName);
    dst->addSupportedComponent(ePixelComponentRGBA);
    dst->setSupportsTiles(false);

    PageParamDescriptor* page = desc.definePageParam("Controls");
    GroupParamDescriptor* grp = nullptr;

    auto place = [&](ParamDescriptor* p) {
        if (grp) p->setParent(*grp);
        page->addChild(*p);
    };
    auto G = [&](const char* n, const char* l, bool open) {
        grp = desc.defineGroupParam(n);
        grp->setLabels(l, l, l);
        grp->setOpen(open);
        page->addChild(*grp);
    };
    auto Dbl = [&](const char* n, const char* l, double def, double mn, double mx, double dmn, double dmx,
                   const char* hint) {
        DoubleParamDescriptor* p = desc.defineDoubleParam(n);
        p->setLabels(l, l, l);
        p->setDefault(def);
        p->setRange(mn, mx);
        p->setDisplayRange(dmn, dmx);
        p->setHint(hint);
        place(p);
    };
    auto Int = [&](const char* n, const char* l, int def, int mn, int mx, const char* hint) {
        IntParamDescriptor* p = desc.defineIntParam(n);
        p->setLabels(l, l, l);
        p->setDefault(def);
        p->setRange(mn, mx);
        p->setDisplayRange(mn, mx);
        p->setHint(hint);
        place(p);
    };
    auto Bool = [&](const char* n, const char* l, bool def, const char* hint) {
        BooleanParamDescriptor* p = desc.defineBooleanParam(n);
        p->setLabels(l, l, l);
        p->setDefault(def);
        p->setHint(hint);
        place(p);
    };
    auto Choice = [&](const char* n, const char* l, std::initializer_list<const char*> opts, int def,
                      const char* hint) {
        ChoiceParamDescriptor* p = desc.defineChoiceParam(n);
        p->setLabels(l, l, l);
        for (const char* o : opts) p->appendOption(o);
        p->setDefault(def);
        p->setHint(hint);
        place(p);
    };
    auto Rgb = [&](const char* n, const char* l, double r, double g, double b) {
        RGBParamDescriptor* p = desc.defineRGBParam(n);
        p->setLabels(l, l, l);
        p->setDefault(r, g, b);
        place(p);
    };

    G("grpPreset", "Preset", true);
    {
        ChoiceParamDescriptor* p = desc.defineChoiceParam("preset");
        p->setLabels("Preset", "Preset", "Preset");
        for (int i = 0; i < kNumPresets; i++) p->appendOption(kPresets[i].name);
        p->setDefault(0);
        p->setHint("Applies a set of values; every parameter stays editable and animatable.");
        p->setAnimates(false);
        place(p);
    }

    G("grpTime", "Time", true);
    Choice("mode", "Render Mode", {"Solid", "Translucent Volume", "Separated Slices", "Cross Section"}, 1,
           "How the time block is rendered.");
    Int("framesBefore", "Frames Before", 24, 0, 63, "Past frames included in the block.");
    Int("framesAfter", "Frames After", 0, 0, 63, "Future frames included in the block.");
    Bool("flip", "Flip Time", false, "Front = oldest frame instead of newest.");
    Bool("loop", "Loop Footage", false, "Wrap time around the clip instead of clamping at its ends.");
    Bool("blend", "Frame Blend", true, "Blends neighbouring frames for smoother volumes (not optical flow).");

    G("grpTransition", "Transition", true);
    Dbl("snap", "Snap to Front", 0, 0, 1, 0, 1, "0 = 3D block, 1 = collapsed flat onto the original video. Animate it to transition in/out.");

    G("grpCamera", "Camera / Shape", true);
    {
        Double2DParamDescriptor* o = desc.defineDouble2DParam("orbit");
        o->setLabels("Orbit Handle", "Orbit Handle", "Orbit Handle");
        o->setDoubleType(eDoubleTypeXYAbsolute);
        o->setDefaultCoordinateSystem(eCoordinatesNormalised);
        o->setDefault(0.5, 0.5);
        o->setHint("Drag this point in the viewer to rotate the cube (centre = no extra rotation).");
        place(o);
    }
    Dbl("depth", "Depth", 1.5, 0.05, 20, 0.2, 5, "Depth of the time block, in image heights.");
    Dbl("yaw", "Yaw", 25, -360, 360, -180, 180, "Orbit around the vertical axis (degrees).");
    Dbl("pitch", "Pitch", 15, -89, 89, -89, 89, "Orbit elevation (degrees).");
    Dbl("roll", "Roll", 0, -360, 360, -180, 180, "Tilts the view around the viewing axis (degrees).");
    Dbl("posX", "Position X", 0, -3, 3, -1, 1, "Moves the cube left/right (fraction of frame width).");
    Dbl("posY", "Position Y", 0, -3, 3, -1, 1, "Moves the cube up/down (fraction of frame height).");
    Dbl("scaleW", "Cube Width", 1, 0.05, 5, 0.1, 3, "Stretches the cube horizontally.");
    Dbl("scaleH", "Cube Height", 1, 0.05, 5, 0.1, 3, "Stretches the cube vertically.");
    Dbl("fov", "Field of View", 35, 5, 120, 10, 90, "Vertical field of view (degrees).");
    Dbl("zoom", "Zoom", 1, 0.05, 20, 0.2, 4, "1 = front face fills the frame at yaw 0 / pitch 0.");
    Dbl("bend", "Bend", 0, 0, 1, 0, 1, "0 = straight stack, 1 = closed time ring.");
    Int("steps", "Quality (sub-slices)", 3, 1, 8, "Sub-slices computed between two frames. Higher = smoother volumes, slower (4-6 for solid sculptures).");
    Int("aa", "Anti-aliasing", 2, 1, 4, "Samples per pixel axis (2 = 4 samples). 1 = fastest.");

    G("grpOpacity", "Opacity / Motion", false);
    Bool("motionOnly", "Motion Only", false, "Removes the static background (temporal median) so only motion leaves a trace.");
    Dbl("motionThresh", "Motion Threshold", 0.06, 0, 1, 0, 0.5, "Difference to the background needed to count as motion.");
    Dbl("motionSoft", "Motion Softness", 0.08, 0, 1, 0, 0.5, "Softness of the motion mask.");
    Choice("motionMethod", "Motion Method", {"Background (median)", "Frame Difference"}, 0,
           "Background: removes the static decor. Frame Difference: keeps what differs from adjacent frames.");
    Choice("opacitySrc", "Opacity From", {"Uniform", "Luma", "Brightness (Max)", "Saturation", "Chroma Key", "Alpha"}, 1,
           "What makes a voxel opaque.");
    Rgb("keyColor", "Key Color", 0, 1, 0);
    Dbl("keyTol", "Key Tolerance", 0.3, 0.01, 2, 0.01, 1, "Chroma key distance.");
    Dbl("gain", "Opacity Gain", 1, 0, 10, 0, 4, "Multiplies the opacity.");
    Bool("invert", "Invert Opacity", false, "");
    Dbl("density", "Density", 6, 0, 200, 0, 40, "Translucent volume density.");
    Dbl("sliceOpacity", "Slice Opacity", 0.9, 0, 1, 0, 1, "Opacity of each card in Separated Slices.");
    Dbl("trailFade", "Trail Fade", 0.3, 0, 1, 0, 1, "Fades older frames.");

    G("grpLook", "Look / Studio", true);
    Dbl("shading", "Shading", 0.6, 0, 1, 0, 1, "Surface lighting on solid blocks and depth shading inside volumes.");
    Dbl("edgeGlow", "Edge Glow", 0, 0, 3, 0, 2, "Neon outline on slices, fresnel rim on solid blocks.");
    Dbl("frost", "Frosted Glass", 0, 0, 1, 0, 1, "Blurs deeper slices (Separated Slices).");
    Bool("floorOn", "Studio Floor", false, "White studio floor with soft shadow and reflection.");
    Rgb("floorColor", "Floor Color", 0.93, 0.93, 0.95);
    Dbl("floorReflect", "Floor Reflection", 0.25, 0, 1, 0, 1, "");
    Dbl("shadowAmt", "Shadow Strength", 0.55, 0, 1, 0, 1, "");
    Dbl("shadowSoft", "Shadow Softness", 0.5, 0, 1, 0, 1, "");
    Dbl("lightYaw", "Light Direction", -35, -180, 180, -180, 180, "Horizontal angle of the key light (degrees).");
    Dbl("lightPitch", "Light Height", 50, 5, 89, 5, 89, "Elevation of the key light (degrees).");

    G("grpStyle", "Style", false);
    Choice("style", "Style", {"Smooth Volume", "Voxel Dust (Point Cloud)", "Scan Lines (Lidar)"}, 0, "Dust and scan lines render on the frame planes.");
    Int("dotGrid", "Dust Grid", 60, 8, 300, "Dust cells per image height.");
    Dbl("dotSize", "Dust Size", 0.7, 0.05, 1.5, 0.05, 1.5, "");
    Dbl("dotDensity", "Dust Density", 0.35, 0, 1, 0, 1, "Fraction of cells that hold a particle.");
    Int("lines", "Scan Lines", 48, 4, 400, "Lines per image height.");
    Dbl("lineWidth", "Line Width", 0.25, 0.02, 1, 0.02, 1, "Lit fraction of each line period.");
    Choice("colorMode", "Color", {"Source", "Time Age Mapping"}, 0, "Age mapping: present -> near past -> far past.");
    Rgb("colorNew", "Present Color", 1, 1, 1);
    Rgb("colorMid", "Near Past Color", 0.2, 0.4, 1);
    Rgb("colorOld", "Far Past Color", 1, 0.2, 0.2);
    Dbl("colorMix", "Age Color Amount", 0.8, 0, 1, 0, 1, "");

    G("grpCross", "Cross Section", false);
    Dbl("crossPos", "Position", 0.5, 0, 1, 0, 1, "Where the plane crosses the time stack.");
    Dbl("crossSlopeX", "Slope X", 1, -4, 4, -2, 2, "Time shift across the image width (0 = plain frame).");
    Dbl("crossSlopeY", "Slope Y", 0, -4, 4, -2, 2, "Time shift across the image height.");

    G("grpFinish", "Finishing", false);
    Dbl("glowAmt", "Glow", 0, 0, 5, 0, 2, "");
    Dbl("glowRad", "Glow Radius", 12, 1, 200, 1, 80, "Pixels.");
    Dbl("glowThresh", "Glow Threshold", 0.6, 0, 2, 0, 1.5, "");
    Dbl("caAmt", "Chromatic Aberration", 0, 0, 50, 0, 20, "Pixels at the image corner.");
    Dbl("grain", "Grain", 0, 0, 1, 0, 0.3, "");
    {
        RGBAParamDescriptor* p = desc.defineRGBAParam("bgColor");
        p->setLabels("Background", "Background", "Background");
        p->setDefault(0, 0, 0, 0);
        p->setHint("Composited behind the volume. Alpha 0 = transparent.");
        place(p);
    }
}

ImageEffect* ChronoVolumeFactory::createInstance(OfxImageEffectHandle handle, ContextEnum) {
    return new ChronoVolumePlugin(handle);
}

} // namespace

namespace OFX {
namespace Plugin {
void getPluginIDs(PluginFactoryArray& ids) {
    static ChronoVolumeFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
} // namespace Plugin
} // namespace OFX
