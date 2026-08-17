// glyphware — COLR (v0/v1) paint graph flattening.
//
// FreeType exposes color glyphs two ways: a composited bitmap (only for the
// formats its renderer handles) and the raw paint graph (FT_Get_Paint & co).
// A vector text engine wants the graph, so it can fill the layers with its own
// rasterizer at any size. This file walks the graph once and hands back a flat
// back-to-front layer list; nothing here knows about any renderer.
#include "glyphware/Face.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H

#include <cmath>
#include <functional>

namespace glyphware {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float fixed16(FT_Fixed v) { return v / 65536.0f; }

// row-major 2x3, see ColorLayer::transform
struct Mat {
    float m[6] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
};

// out = a * b  (apply b first, then a)
Mat concat(const Mat& a, const Mat& b) {
    Mat r;
    r.m[0] = a.m[0] * b.m[0] + a.m[1] * b.m[3];
    r.m[1] = a.m[0] * b.m[1] + a.m[1] * b.m[4];
    r.m[2] = a.m[0] * b.m[2] + a.m[1] * b.m[5] + a.m[2];
    r.m[3] = a.m[3] * b.m[0] + a.m[4] * b.m[3];
    r.m[4] = a.m[3] * b.m[1] + a.m[4] * b.m[4];
    r.m[5] = a.m[3] * b.m[2] + a.m[4] * b.m[5] + a.m[5];
    return r;
}

} // namespace

bool Face::colorLayers(GlyphId gid, std::vector<ColorLayer>& out, ColorGlyphBox* box) const {
    LibraryLock lock(*lib_);

    FT_OpaquePaint root;
    root.p = nullptr;
    root.insert_root_transform = 0;
    if (!FT_Get_Color_Glyph_Paint(face_, gid, FT_COLOR_INCLUDE_ROOT_TRANSFORM, &root))
        return false;

    // CPAL palette 0. A missing palette is not fatal: paint entries can still
    // carry the "use text foreground color" index (0xFFFF), which we resolve to
    // opaque black here — the consumer overrides it with its own fill color.
    FT_Color* palette = nullptr;
    FT_Palette_Select(face_, 0, &palette);

    const auto resolveColor = [&](const FT_ColorIndex& ci, ColorPaint& paint) {
        std::uint8_t r = 0, g = 0, b = 0, a = 255;
        if (palette && ci.palette_index != 0xFFFF) {
            const FT_Color c = palette[ci.palette_index];
            r = c.red; g = c.green; b = c.blue; a = c.alpha;
        }
        const float alphaScale = ci.alpha / 16384.0f;   // F2Dot14
        float av = a * alphaScale;
        if (av < 0.f) av = 0.f;
        if (av > 255.f) av = 255.f;
        paint.r = r; paint.g = g; paint.b = b;
        paint.a = static_cast<std::uint8_t>(av);
    };

    const auto readStops = [&](FT_ColorLine& line, ColorPaint& paint) {
        FT_ColorStop stop;
        while (FT_Get_Colorline_Stops(face_, &stop, &line.color_stop_iterator)) {
            ColorPaint tmp;
            resolveColor(stop.color, tmp);
            ColorStop s;
            s.offset = fixed16(stop.stop_offset);
            if (s.offset < 0.f) s.offset = 0.f;
            if (s.offset > 1.f) s.offset = 1.f;
            s.r = tmp.r; s.g = tmp.g; s.b = tmp.b; s.a = tmp.a;
            paint.stops.push_back(s);
        }
    };

    // Resolve a leaf paint (the child of a PaintGlyph) into a fill. Returns
    // false for anything that is not a fill — the caller then recurses.
    const auto resolveFill = [&](const FT_COLR_Paint& p, ColorPaint& paint) {
        switch (p.format) {
        case FT_COLR_PAINTFORMAT_SOLID:
            paint.kind = PaintKind::Solid;
            resolveColor(p.u.solid.color, paint);
            return true;
        case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT: {
            paint.kind = PaintKind::LinearGradient;
            const auto& lg = p.u.linear_gradient;
            paint.x0 = fixed16(lg.p0.x); paint.y0 = fixed16(lg.p0.y);
            paint.x1 = fixed16(lg.p1.x); paint.y1 = fixed16(lg.p1.y);
            FT_ColorLine line = lg.colorline;
            readStops(line, paint);
            return true;
        }
        case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT: {
            paint.kind = PaintKind::RadialGradient;
            const auto& rg = p.u.radial_gradient;
            paint.x0 = fixed16(rg.c0.x); paint.y0 = fixed16(rg.c0.y);
            paint.r0 = fixed16(rg.r0);
            paint.x1 = fixed16(rg.c1.x); paint.y1 = fixed16(rg.c1.y);
            paint.r1 = fixed16(rg.r1);
            FT_ColorLine line = rg.colorline;
            readStops(line, paint);
            return true;
        }
        default:
            return false;
        }
    };

    std::function<void(FT_OpaquePaint, const Mat&)> traverse =
        [&](FT_OpaquePaint opaque, const Mat& ctm) {
        FT_COLR_Paint p;
        if (!FT_Get_Paint(face_, opaque, &p)) return;

        switch (p.format) {
        case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
            FT_OpaquePaint layer;
            layer.p = nullptr;
            layer.insert_root_transform = 0;
            while (FT_Get_Paint_Layers(face_, &p.u.colr_layers.layer_iterator, &layer))
                traverse(layer, ctm);
            break;
        }

        case FT_COLR_PAINTFORMAT_GLYPH: {
            FT_COLR_Paint child;
            if (!FT_Get_Paint(face_, p.u.glyph.paint, &child)) break;
            ColorLayer out_layer;
            out_layer.gid = p.u.glyph.glyphID;
            for (int i = 0; i < 6; i++) out_layer.transform[i] = ctm.m[i];
            if (!resolveFill(child, out_layer.paint)) {
                // Nested paint under a glyph (e.g. a composite). Emit the shape
                // opaque black so it is at least visible, matching what a plain
                // COLRv0 consumer would draw.
                out_layer.paint.kind = PaintKind::Solid;
                out_layer.paint.r = out_layer.paint.g = out_layer.paint.b = 0;
                out_layer.paint.a = 255;
            }
            out.push_back(std::move(out_layer));
            break;
        }

        case FT_COLR_PAINTFORMAT_COLR_GLYPH: {
            FT_OpaquePaint sub;
            sub.p = nullptr;
            sub.insert_root_transform = 0;
            if (FT_Get_Color_Glyph_Paint(face_, p.u.colr_glyph.glyphID,
                                         FT_COLOR_NO_ROOT_TRANSFORM, &sub)) {
                traverse(sub, ctm);
            }
            break;
        }

        case FT_COLR_PAINTFORMAT_TRANSFORM: {
            const auto& t = p.u.transform.affine;
            Mat m;
            m.m[0] = fixed16(t.xx); m.m[1] = fixed16(t.xy); m.m[2] = fixed16(t.dx);
            m.m[3] = fixed16(t.yx); m.m[4] = fixed16(t.yy); m.m[5] = fixed16(t.dy);
            traverse(p.u.transform.paint, concat(ctm, m));
            break;
        }

        case FT_COLR_PAINTFORMAT_TRANSLATE: {
            Mat m;
            m.m[2] = fixed16(p.u.translate.dx);
            m.m[5] = fixed16(p.u.translate.dy);
            traverse(p.u.translate.paint, concat(ctm, m));
            break;
        }

        case FT_COLR_PAINTFORMAT_SCALE: {
            const auto& s = p.u.scale;
            const float sx = fixed16(s.scale_x), sy = fixed16(s.scale_y);
            const float cx = fixed16(s.center_x), cy = fixed16(s.center_y);
            // translate(c) * scale(s) * translate(-c)
            Mat m;
            m.m[0] = sx; m.m[2] = cx * (1.f - sx);
            m.m[4] = sy; m.m[5] = cy * (1.f - sy);
            traverse(s.paint, concat(ctm, m));
            break;
        }

        case FT_COLR_PAINTFORMAT_ROTATE: {
            const auto& r = p.u.rotate;
            const float angle = fixed16(r.angle) * 2.f * kPi;   // turns -> radians
            const float ca = std::cos(angle), sa = std::sin(angle);
            const float cx = fixed16(r.center_x), cy = fixed16(r.center_y);
            Mat m;
            m.m[0] = ca; m.m[1] = -sa; m.m[2] = cx - ca * cx + sa * cy;
            m.m[3] = sa; m.m[4] = ca;  m.m[5] = cy - sa * cx - ca * cy;
            traverse(r.paint, concat(ctm, m));
            break;
        }

        case FT_COLR_PAINTFORMAT_SKEW: {
            const auto& sk = p.u.skew;
            const float tx = std::tan(fixed16(sk.x_skew_angle) * 2.f * kPi);
            const float ty = std::tan(fixed16(sk.y_skew_angle) * 2.f * kPi);
            Mat m;
            m.m[1] = tx;
            m.m[3] = ty;
            traverse(sk.paint, concat(ctm, m));
            break;
        }

        case FT_COLR_PAINTFORMAT_COMPOSITE:
            // No compositing modes yet: draw backdrop then source (SRC_OVER).
            traverse(p.u.composite.backdrop_paint, ctm);
            traverse(p.u.composite.source_paint, ctm);
            break;

        default:
            break;
        }
    };

    out.clear();
    traverse(root, Mat());

    if (box) {
        FT_ClipBox cb;
        if (FT_Get_Color_Glyph_ClipBox(face_, gid, &cb)) {
            // 26.6 pixels, four corners (not axis-aligned in general)
            const float xs[4] = {cb.bottom_left.x / 64.0f, cb.top_left.x / 64.0f,
                                 cb.top_right.x / 64.0f, cb.bottom_right.x / 64.0f};
            const float ys[4] = {cb.bottom_left.y / 64.0f, cb.top_left.y / 64.0f,
                                 cb.top_right.y / 64.0f, cb.bottom_right.y / 64.0f};
            box->xMin = box->xMax = xs[0];
            box->yMin = box->yMax = ys[0];
            for (int i = 1; i < 4; i++) {
                if (xs[i] < box->xMin) box->xMin = xs[i];
                if (xs[i] > box->xMax) box->xMax = xs[i];
                if (ys[i] < box->yMin) box->yMin = ys[i];
                if (ys[i] > box->yMax) box->yMax = ys[i];
            }
            box->valid = true;
        } else {
            box->valid = false;
        }
    }

    return !out.empty();
}

} // namespace glyphware
