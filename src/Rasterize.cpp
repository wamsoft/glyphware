// glyphware — outline rasterization (coverage masks).
//
// The consumer hands in an affine (font units → pixels) and gets back an 8-bit
// coverage mask, filled or stroked. Everything is done on the UNSCALED outline
// and the affine is baked into it, so oblique / condensed / mirrored / sub-pixel
// placements are exact instead of being approximated after the fact.
//
// FreeType's smooth rasterizer and FT_Stroker do the actual work: they are the
// same code paths the engine's classic text path uses, which is the point —
// text drawn through glyphware looks identical to text drawn by the host.
#include "glyphware/Face.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_SYNTHESIS_H

#include <cmath>

namespace glyphware {
namespace {

// float -> 16.16
FT_Fixed toFixed16(double v) { return static_cast<FT_Fixed>(std::lround(v * 65536.0)); }
// float -> 26.6
FT_Pos toF26Dot6(double v) { return static_cast<FT_Pos>(std::lround(v * 64.0)); }

FT_Stroker_LineJoin toFtJoin(StrokeJoin j) {
    switch (j) {
    case StrokeJoin::Bevel: return FT_STROKER_LINEJOIN_BEVEL;
    case StrokeJoin::Miter: return FT_STROKER_LINEJOIN_MITER_FIXED;
    case StrokeJoin::Round:
    default: return FT_STROKER_LINEJOIN_ROUND;
    }
}

FT_Stroker_LineCap toFtCap(StrokeCap c) {
    switch (c) {
    case StrokeCap::Square: return FT_STROKER_LINECAP_SQUARE;
    case StrokeCap::Butt: return FT_STROKER_LINECAP_BUTT;
    case StrokeCap::Round:
    default: return FT_STROKER_LINECAP_ROUND;
    }
}

// RAII for an FT_Outline we allocated ourselves (the stroker result).
struct OwnedOutline {
    FT_Library lib;
    FT_Outline outline{};
    bool valid = false;
    explicit OwnedOutline(FT_Library l) : lib(l) {}
    ~OwnedOutline() { if (valid) FT_Outline_Done(lib, &outline); }
};

struct OwnedStroker {
    FT_Stroker stroker = nullptr;
    ~OwnedStroker() { if (stroker) FT_Stroker_Done(stroker); }
};

} // namespace

bool Face::renderGlyphMask(GlyphId gid, const RenderParams& params, GlyphMask& out) {
    LibraryLock lock(*lib_);

    // Unscaled outline: the affine below carries the size, so nothing is scaled
    // twice and no ppem rounding creeps in.
    if (FT_Load_Glyph(face_, gid,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
        return false;
    }
    FT_GlyphSlot slot = face_->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE) return false;   // bitmap-only glyph

    if (params.bold) FT_GlyphSlot_Embolden(slot);
    if (params.italic) FT_GlyphSlot_Oblique(slot);

    FT_Outline outline = slot->outline;

    // font units -> 26.6 pixels. FT_Outline_Transform keeps the input unit, so
    // fold the 64 of 26.6 into the matrix and feed the translation in 26.6.
    const Transform2D& t = params.transform;
    FT_Matrix m;
    m.xx = toFixed16(t.xx * 64.0);
    m.xy = toFixed16(t.xy * 64.0);
    m.yx = toFixed16(t.yx * 64.0);
    m.yy = toFixed16(t.yy * 64.0);
    FT_Outline_Transform(&outline, &m);
    FT_Outline_Translate(&outline, toF26Dot6(t.dx), toF26Dot6(t.dy));

    // Stroking replaces the shape with its border, so the glyph interior stays
    // empty — that is what "outlined text" means.
    OwnedOutline stroked(lib_->ft());
    if (params.strokeWidth > 0.f) {
        OwnedStroker st;
        if (FT_Stroker_New(lib_->ft(), &st.stroker) != 0) return false;
        FT_Stroker_Set(st.stroker, toF26Dot6(params.strokeWidth / 2.0),
                       toFtCap(params.cap), toFtJoin(params.join),
                       toFixed16(params.miterLimit));
        if (FT_Stroker_ParseOutline(st.stroker, &outline, /*opened=*/0) != 0) return false;

        FT_UInt points = 0, contours = 0;
        if (FT_Stroker_GetCounts(st.stroker, &points, &contours) != 0) return false;
        if (points == 0 || contours == 0) return false;
        if (FT_Outline_New(lib_->ft(), points, static_cast<FT_Int>(contours),
                           &stroked.outline) != 0) {
            return false;
        }
        stroked.valid = true;
        stroked.outline.n_points = 0;
        stroked.outline.n_contours = 0;
        FT_Stroker_Export(st.stroker, &stroked.outline);
        outline = stroked.outline;
    }

    // Grid-fit the control box to whole pixels; the rasterizer needs the outline
    // moved into the mask's own coordinate space.
    FT_BBox cbox;
    FT_Outline_Get_CBox(&outline, &cbox);
    const FT_Pos xMin = cbox.xMin & ~63;
    const FT_Pos yMin = cbox.yMin & ~63;
    const FT_Pos xMax = (cbox.xMax + 63) & ~63;
    const FT_Pos yMax = (cbox.yMax + 63) & ~63;

    const int width = static_cast<int>((xMax - xMin) >> 6);
    const int rows = static_cast<int>((yMax - yMin) >> 6);
    if (width <= 0 || rows <= 0) return false;
    // A mask is a scratch buffer; refuse absurd sizes rather than allocate them.
    if (width > 8192 || rows > 8192) return false;

    FT_Outline_Translate(&outline, -xMin, -yMin);

    maskBuf_.assign(static_cast<std::size_t>(width) * rows, 0);

    FT_Bitmap bitmap{};
    bitmap.width = static_cast<unsigned int>(width);
    bitmap.rows = static_cast<unsigned int>(rows);
    bitmap.pitch = width;
    bitmap.num_grays = 256;
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    bitmap.buffer = maskBuf_.data();

    if (FT_Outline_Get_Bitmap(lib_->ft(), &outline, &bitmap) != 0) return false;

    out.left = static_cast<int>(xMin >> 6);
    out.top = static_cast<int>(yMax >> 6);
    out.width = width;
    out.rows = rows;
    out.pitch = width;
    out.buffer = maskBuf_.data();
    return true;
}

} // namespace glyphware
