// glyphware — single-line text layout.
//
// Ties the pieces together the way a text-drawing consumer needs: take one line
// of logical-order UTF-8, run BiDi to get visual runs, itemize each run across a
// fallback face chain (per-codepoint coverage), shape each item with HarfBuzz,
// and emit positioned glyphs in visual (left-to-right) order. This is what the
// core drawText path and the ThorVG text backend both consume.
//
// Line/word breaking is intentionally NOT here — a caller breaks text into lines
// first (a future break provider), then lays out each line with this.
#ifndef GLYPHWARE_LAYOUT_H
#define GLYPHWARE_LAYOUT_H

#include "Bidi.h"
#include "Face.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace glyphware {

// One shaped glyph placed on the line. `x`/`y` are the pen position (baseline
// origin) in pixels; `xOffset`/`yOffset` are the shaper's per-glyph offsets.
struct PositionedGlyph {
    Face* face = nullptr;       // borrowed (the caller owns the chain)
    GlyphId gid = 0;
    float x = 0.f;
    float y = 0.f;
    float xOffset = 0.f;
    float yOffset = 0.f;
    float advance = 0.f;
    std::uint32_t cluster = 0;  // byte offset into the source UTF-8
    bool rtl = false;
};

struct LineLayout {
    std::vector<PositionedGlyph> glyphs;  // visual order, x increasing
    float width = 0.f;
    float ascent = 0.f;
    float descent = 0.f;
};

// Lay out one line. `chain` is the fallback order tried per codepoint; chain[0]
// is the primary (used for line metrics and for uncovered codepoints → .notdef).
// All chain faces are set to `pixelSize`.
LineLayout layoutLine(std::string_view utf8, BaseDirection base,
                      const std::vector<std::shared_ptr<Face>>& chain, int pixelSize);

} // namespace glyphware

#endif // GLYPHWARE_LAYOUT_H
