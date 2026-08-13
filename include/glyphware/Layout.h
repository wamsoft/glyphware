// glyphware — text layout.
//
// Two levels, both consumed by the 吉里吉里Z core drawText path and by the
// ThorVG/Elements text backend:
//
//   layoutLine  — one line of logical-order UTF-8: run BiDi to get visual runs,
//                 itemize each run across a fallback face chain (per-codepoint
//                 coverage), shape each item with HarfBuzz, emit positioned
//                 glyphs in visual (left-to-right) order.
//   layoutBlock — flow text into a rectangle: explicit newlines, greedy word /
//                 per-character (CJK) wrapping with 禁則 (kinsoku), alignment,
//                 and a cluster `count` limit for typewriter reveals.
#ifndef GLYPHWARE_LAYOUT_H
#define GLYPHWARE_LAYOUT_H

#include "Bidi.h"
#include "Face.h"

#include <cstddef>
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

//---------------------------------------------------------------------------
// clusters
//
// A "cluster" is one draw-time unit: whatever the shaper gave a shared cluster
// value (a ligature, a base + its combining marks, an emoji ZWJ sequence).
// Cluster values are byte offsets into the shaped text, so ascending cluster
// order is logical (reading) order — which is what a typewriter reveal wants
// even when the visual order is mixed by BiDi.
//---------------------------------------------------------------------------

// Number of distinct clusters in a laid-out line.
int lineClusterCount(const LineLayout& line);

// Drop everything past the first `count` clusters in logical order (`count` < 0
// keeps the line intact). Returns the number of clusters kept, so a caller can
// tell whether the line was actually truncated (kept < lineClusterCount before
// the call). `line.width` is deliberately left at the full line width: a reveal
// must not make the remaining text shift or re-align as it advances.
int limitClusters(LineLayout& line, int count);

// Total clusters of a whole text, newlines splitting it into lines and not
// being counted themselves. This is the number a `count` reveal counts up to.
int countClusters(std::string_view utf8, BaseDirection base,
                  const std::vector<std::shared_ptr<Face>>& chain, int pixelSize);

//---------------------------------------------------------------------------
// block layout (rectangle flow)
//---------------------------------------------------------------------------

enum class Align { Left = 0, Center = 1, Right = 2 };

struct BlockOptions {
    float width = 0.f;         // wrap width in px; <= 0 wraps only at newlines
    float height = 0.f;        // px; <= 0 = unbounded. Lines that would cross
                               // the bottom are dropped (not clipped)
    float lineSpacing = 0.f;   // extra px between lines (may be negative)
    Align align = Align::Left;
    int count = -1;            // cluster limit across all lines; < 0 = no limit
};

// One laid-out line of a block. Glyph positions inside `layout` are relative to
// the line's own origin, so a consumer draws it with its baseline origin at
// (blockX + x, blockY + y). `byteStart`/`byteEnd` are the line's range in the
// source UTF-8 (trailing spaces already trimmed off `byteEnd`), and glyph
// cluster values are byte offsets relative to `byteStart`.
struct BlockLine {
    LineLayout layout;
    float x = 0.f;                 // alignment offset from the block's left
    float y = 0.f;                 // baseline offset from the block's top
    std::size_t byteStart = 0;
    std::size_t byteEnd = 0;
    int clusters = 0;              // clusters kept after the `count` limit
    int totalClusters = 0;         // clusters this line has without the limit
};

struct BlockLayout {
    std::vector<BlockLine> lines;
    float width = 0.f;        // widest line (full width, ignoring `count`)
    float height = 0.f;       // top of the first line to the bottom of the last
    float lineHeight = 0.f;   // baseline-to-baseline pitch (lineSpacing folded in)
    float ascent = 0.f;
    float descent = 0.f;
    int lineCount = 0;        // == lines.size()
    int drawnClusters = 0;    // clusters actually laid out (after `count`)
    int totalClusters = 0;    // clusters those same lines hold without `count`
};

// Flow `utf8` into a rectangle of `opts.width` x `opts.height`.
//
// '\n' ('\r\n' / '\r') breaks explicitly; space-separated scripts wrap word-wise
// and CJK per character, with simple 行頭/行末禁則. Wrapping is always resolved
// for the FULL text before `count` is applied, so a typewriter reveal never
// reflows. A unit wider than the area falls back to per-character splitting, so
// progress is guaranteed.
BlockLayout layoutBlock(std::string_view utf8, BaseDirection base,
                        const std::vector<std::shared_ptr<Face>>& chain,
                        int pixelSize, const BlockOptions& opts);

} // namespace glyphware

#endif // GLYPHWARE_LAYOUT_H
