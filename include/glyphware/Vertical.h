// glyphware — vertical (top-to-bottom) line layout.
//
// The counterpart of layoutLine() for Japanese vertical writing. It is a
// separate entry rather than a flag on layoutLine because the two produce
// different geometry: a vertical line has no BiDi, its glyphs advance along a
// column, and part of them (the Latin stretches) are laid out horizontally and
// then tipped 90 degrees into the column.
//
//   upright runs (CJK)   — shaped with Direction::TTB, so HarfBuzz applies the
//                          `vert` / `vrt2` vertical forms and takes advances and
//                          origins from `vmtx` / `VORG`
//   sideways runs (Latin) — shaped LTR and rotated into the column
//
// Everything above this — the JLReq character classes, the inter-class spacing,
// kinsoku and line breaking — is typesetting policy and lives in the consumer.
// This layer only answers "which glyph goes where in one unbroken column".
#ifndef GLYPHWARE_VERTICAL_H
#define GLYPHWARE_VERTICAL_H

#include "Face.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace glyphware {

// Which characters stand upright in a vertical line (CSS text-orientation).
enum class TextOrientation {
    Mixed,      // CJK upright, Latin sideways (the default)
    Upright,    // everything upright
    Sideways,   // everything sideways
};

enum class CharOrientation { Upright, Rotated };

// UAX #50 Vertical_Orientation, collapsed to two values: U / Tu / Tr come back
// Upright, R comes back Rotated. Tu and Tr (the ones that are meant to be
// replaced by a vertical form and then stand upright — brackets, punctuation,
// wave dashes) land on Upright because the TTB shaping below runs `vert`/`vrt2`
// for them. A font without vertical forms leaves them plainly upright, which is
// what those characters want anyway.
CharOrientation charOrientation(char32_t cp);

// Rotation of a sideways glyph, in radians, counterclockwise positive in the
// mathematical (y-up) convention. A y-down device therefore tips the glyph
// clockwise by 90 degrees — Latin text read with the head tilted to the right.
inline constexpr float kSidewaysRotation = -1.5707963267948966f;

// One placed glyph of a vertical line. The coordinates are the column-local
// pair used throughout vertical typesetting:
//
//   u — across the line: 0 is the vertical baseline (the column's centre
//       line), positive to the right
//   v — along the line: 0 is the line head (the top), positive downward
//
// A consumer draws the glyph at (columnCentreX + u, lineHeadY + v), applying
// `rotation` about that point.
struct VerticalGlyph {
    Face* face = nullptr;        // borrowed (the caller owns the chain)
    GlyphId gid = 0;
    float u = 0.f;
    float v = 0.f;
    float advance = 0.f;         // this glyph's share of the line advance
    float rotation = 0.f;        // 0 or kSidewaysRotation
    std::uint32_t cluster = 0;   // byte offset into the source UTF-8
};

// One shaping cluster — the unit the typesetting layer above works in. It never
// moves individual glyphs: it decides a new `v` for the cluster and shifts the
// cluster's glyphs by the delta from `origin`.
struct VerticalCluster {
    std::uint32_t glyphStart = 0;   // index into VerticalLineLayout::glyphs
    std::uint32_t glyphCount = 0;
    std::size_t byteStart = 0;      // range in the source UTF-8
    std::size_t byteEnd = 0;
    float origin = 0.f;             // v of the cluster as laid out solid here
    float advance = 0.f;            // advance the shaper returned
    char32_t lead = 0;              // leading codepoint (for character classing)
    bool upright = true;
};

struct VerticalLineLayout {
    std::vector<VerticalGlyph> glyphs;
    std::vector<VerticalCluster> clusters;
    float advance = 0.f;        // total advance along the column
    float extentLeft = 0.f;     // furthest left of the vertical baseline (<= 0)
    float extentRight = 0.f;    // furthest right (>= 0)

    float width() const { return extentRight - extentLeft; }
};

// Lay out one unbroken vertical line (no newlines) solid — no inter-class
// spacing, no kinsoku, no line breaking. `chain` is the fallback order tried
// per codepoint, exactly as in layoutLine(); all its faces are set to
// `pixelSize`.
VerticalLineLayout layoutVerticalLine(std::string_view utf8,
                                      const std::vector<std::shared_ptr<Face>>& chain,
                                      int pixelSize,
                                      TextOrientation orientation = TextOrientation::Mixed);

} // namespace glyphware

#endif // GLYPHWARE_VERTICAL_H
