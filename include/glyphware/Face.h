// glyphware — a single font face (font file + faceIndex).
//
// A Face owns an FT_Face over a host-supplied FontBlob (kept alive here), plus
// a lazily-created HarfBuzz font for shaping. It exposes both representations a
// consumer might need: vector OUTLINES (for ThorVG / vector text) and rendered
// BITMAPS (for the bitmap drawText path), from the one shared FreeType face.
#ifndef GLYPHWARE_FACE_H
#define GLYPHWARE_FACE_H

#include "Blob.h"
#include "Descriptor.h"
#include "Library.h"

#include <cstdint>
#include <memory>
#include <vector>

typedef struct FT_FaceRec_* FT_Face;
struct hb_font_t;

namespace glyphware {

using GlyphId = std::uint32_t;

// Per-glyph geometry / advance in 26.6 or integer pixels depending on call.
struct GlyphMetrics {
    float advanceX = 0.f;   // pixels (or font units for unscaled)
    float advanceY = 0.f;
    float bearingX = 0.f;
    float bearingY = 0.f;
    float width = 0.f;
    float height = 0.f;
};

// Face-wide line metrics at the current pixel size (see SetPixelSize).
// underline/strikeout offsets are distances from the baseline, positive downward
// (so a typical underline offset is a small positive number).
struct LineMetrics {
    float ascent = 0.f;
    float descent = 0.f;   // positive downward extent
    float lineGap = 0.f;
    float unitsPerEm = 0.f;
    float ascenderUnits = 0.f;  // raw FT_FaceRec::ascender in font units (unscaled)
    float descenderUnits = 0.f; // raw FT_FaceRec::descender in font units (negative, FT convention)
    float heightUnits = 0.f;    // raw FT_FaceRec::height in font units (baseline-to-baseline)
    float ppemY = 0.f;          // active vertical pixels-per-EM (size->metrics.y_ppem)
    float underlineOffset = 0.f;
    float underlineThickness = 0.f;
    float strikeoutOffset = 0.f;
    float strikeoutThickness = 0.f;
};

// Rendered glyph bitmap. `format` distinguishes gray (1 byte/px) from BGRA
// color emoji (4 bytes/px). Buffer is owned by the caller-provided storage.
enum class BitmapFormat { Gray, Mono, BGRA };
struct GlyphBitmap {
    BitmapFormat format = BitmapFormat::Gray;
    int left = 0;      // bearing to bitmap origin
    int top = 0;
    int width = 0;
    int rows = 0;
    int pitch = 0;     // bytes per row (may be negative for bottom-up)
    const std::uint8_t* buffer = nullptr;  // valid until next glyph call on this Face
};

// Sink for outline decomposition. Coordinates are in font units when loaded
// unscaled (the default for the vector path), y-up as FreeType stores them;
// the ThorVG backend flips to y-down itself.
class OutlineSink {
public:
    virtual ~OutlineSink() = default;
    virtual void moveTo(float x, float y) = 0;
    virtual void lineTo(float x, float y) = 0;
    virtual void quadTo(float cx, float cy, float x, float y) = 0;   // conic
    virtual void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) = 0;
    virtual void close() = 0;
};

// A variable-font axis coordinate (fvar design space), e.g. {'wght', 700}.
struct VarCoord {
    std::uint32_t tag = 0;   // big-endian packed tag, see VarAxis::tag
    float value = 0.f;
};

// Hinting mode for scaled glyph metrics / bitmaps.
//   Hinted   — FreeType's default: the outline is grid-fitted and the advance is
//              rounded to whole pixels. Right for a bitmap draw path.
//   Unhinted — linearly scaled from font units, fractional advances preserved.
//              Right for a layout engine (minikin / HarfBuzz style pipelines):
//              hinted advances quantize and text drifts as the line grows.
enum class Hinting { Hinted, Unhinted };

class Face {
public:
    // Open face `index` from `blob`. Returns nullptr on failure. The blob is
    // retained for the Face's lifetime.
    static std::shared_ptr<Face> open(std::shared_ptr<FontBlob> blob,
                                      std::string key, int index = 0);

    ~Face();
    Face(const Face&) = delete;
    Face& operator=(const Face&) = delete;

    const FontDescriptor& descriptor() const noexcept { return desc_; }
    FT_Face ft() const noexcept { return face_; }
    hb_font_t* hb();   // lazily created HarfBuzz font (unscaled, font units)

    // The host-supplied bytes backing this face, and the face index within them.
    // A consumer that runs its own shaper over the raw SFNT (e.g. minikin, which
    // builds its own hb_face from the font data) reads them here instead of
    // asking the host to load the font a second time.
    const std::shared_ptr<FontBlob>& blob() const noexcept { return blob_; }
    const std::uint8_t* data() const noexcept { return blob_ ? blob_->data() : nullptr; }
    std::size_t size() const noexcept { return blob_ ? blob_->size() : 0; }
    int faceIndex() const noexcept { return desc_.faceIndex; }

    // Set working pixel size (affects bitmap render + scaled metrics).
    bool setPixelSize(int pixels);

    // Codepoint -> glyph id (0 = .notdef / absent).
    GlyphId glyphIndex(char32_t codepoint) const;
    bool covers(char32_t codepoint) const { return glyphIndex(codepoint) != 0; }

    // Advance/bearing/bbox for a glyph, in current pixel size. `bold`/`italic`
    // apply synthetic emboldening/obliquing so the advance matches glyphBitmap().
    // `hinting` picks grid-fitted (default, matches glyphBitmap) or linearly
    // scaled metrics — see Hinting.
    bool glyphMetrics(GlyphId gid, GlyphMetrics& out, bool bold = false, bool italic = false,
                      Hinting hinting = Hinting::Hinted) const;

    // Advance/bearing/bbox in FONT UNITS (size-independent, never hinted), so a
    // caller can cache one value per glyph and scale it by pixelSize/unitsPerEm.
    // false for bitmap-only faces (no outline to measure) — use glyphMetrics().
    bool glyphMetricsUnscaled(GlyphId gid, GlyphMetrics& out,
                              bool bold = false, bool italic = false) const;

    LineMetrics lineMetrics() const;

    // Correction factor from the selected fixed strike to the requested pixel
    // size for bitmap-only fonts (CBDT color emoji etc.); 1.0 for scalable
    // fonts. HarfBuzz advances/offsets come back at the strike ppem, so the
    // shaper multiplies its output by this to match glyphBitmap()/glyphMetrics().
    float fixedStrikeScale() const;

    // Decompose the glyph outline (font units, unscaled). false if the glyph
    // has no outline (e.g. a bitmap-only color emoji). `bold`/`italic` apply
    // synthetic emboldening / obliquing.
    bool glyphOutline(GlyphId gid, OutlineSink& sink, bool bold = false, bool italic = false) const;

    // Render the glyph to a bitmap at the current pixel size. `color` requests
    // the CBDT/COLR/sbix BGRA path when available. `bold`/`italic` apply
    // synthetic emboldening / obliquing (ignored for color bitmaps). The
    // returned buffer is owned by this Face and valid until the next render call.
    bool glyphBitmap(GlyphId gid, bool color, GlyphBitmap& out, bool bold = false, bool italic = false);

    // Set a 2x2 transform (FreeType y-up coords) applied to subsequently loaded
    // glyphs — for rotated/sheared text. clearTransform() resets to identity.
    void setTransform(double xx, double xy, double yx, double yy);
    void clearTransform();

    // ---- variable fonts (fvar) ----------------------------------------------
    // The available axes are in descriptor().axes. Coordinates are FACE STATE:
    // every glyph call after this uses them, so a consumer that wants two
    // instances of one variable font (e.g. Regular + Bold from a wght axis) must
    // open two Faces rather than share one.

    // Set design coordinates for the listed axes; axes not listed keep their
    // current value. Values are clamped to each axis' min/max. Returns false when
    // the face has no fvar table or none of the tags matched.
    bool setVariations(const std::vector<VarCoord>& coords);

    // Current design coordinates (empty for a non-variable face).
    std::vector<VarCoord> variations() const;

    // Range of one axis in design units. false when the axis is absent.
    bool axisRange(std::uint32_t tag, float& minValue, float& defaultValue,
                   float& maxValue) const;

private:
    Face() = default;
    std::shared_ptr<Library> lib_;
    std::shared_ptr<FontBlob> blob_;
    FT_Face face_ = nullptr;
    hb_font_t* hb_ = nullptr;
    FontDescriptor desc_;
    int pixelSize_ = 0;
    // owned normalized bitmap (mono->gray / num_grays->256 / scaled color strike);
    // glyphBitmap() points GlyphBitmap::buffer here, valid until the next call.
    std::vector<std::uint8_t> bmpBuf_;

    void resolveMetadata();
};

} // namespace glyphware

#endif // GLYPHWARE_FACE_H
