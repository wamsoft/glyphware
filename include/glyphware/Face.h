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

    // Set working pixel size (affects bitmap render + scaled metrics).
    bool setPixelSize(int pixels);

    // Codepoint -> glyph id (0 = .notdef / absent).
    GlyphId glyphIndex(char32_t codepoint) const;
    bool covers(char32_t codepoint) const { return glyphIndex(codepoint) != 0; }

    // Advance/bearing/bbox for a glyph, in current pixel size. `bold`/`italic`
    // apply synthetic emboldening/obliquing so the advance matches glyphBitmap().
    bool glyphMetrics(GlyphId gid, GlyphMetrics& out, bool bold = false, bool italic = false) const;
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
