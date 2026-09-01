// glyphware — HarfBuzz text shaping over one Face (single run).
//
// This is the one shaping implementation the whole stack shares. A run is a
// stretch of text in a single face/script/direction; higher layers (fallback
// chain, itemization) call this per resolved face. Output carries glyph ids +
// positioned advances/offsets + cluster (byte offset into the run's UTF-8),
// which is exactly what both the bitmap draw path and the ThorVG outline path
// need.
#ifndef GLYPHWARE_SHAPER_H
#define GLYPHWARE_SHAPER_H

#include "Face.h"

#include <string>
#include <string_view>
#include <vector>

namespace glyphware {

// TTB is the vertical (top-to-bottom) writing direction: HarfBuzz then applies
// the `vert` / `vrt2` vertical form substitutions and takes advances/origins
// from `vmtx` / `VORG`, which is what upright CJK runs need. See Vertical.h.
enum class Direction { LTR, RTL, TTB };

struct ShapedGlyph {
    GlyphId gid = 0;
    float xAdvance = 0.f;   // pixels at the face's current size
    float yAdvance = 0.f;
    float xOffset = 0.f;
    float yOffset = 0.f;
    std::uint32_t cluster = 0;  // byte offset into the source UTF-8
};

struct ShapeOptions {
    std::string language;   // BCP47 (e.g. "ja", "zh-Hans"); "" = guess
    std::string script;     // ISO-15924 (e.g. "Hani"); "" = guess
    Direction direction = Direction::LTR;
    // Guess script/language/direction from the content. `direction` is still
    // honoured for TTB, which no guess would ever produce.
    bool guessSegmentProperties = true;  // parity with the current ThorVG path
};

// Shape one UTF-8 run with `face` (uses the face's current pixel size for
// advances). Appends to `out`.
void shapeRun(Face& face, std::string_view utf8, const ShapeOptions& opts,
              std::vector<ShapedGlyph>& out);

} // namespace glyphware

#endif // GLYPHWARE_SHAPER_H
