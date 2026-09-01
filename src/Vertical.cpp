#include "glyphware/Vertical.h"
#include "glyphware/Shaper.h"
#include "Utf8.h"

#include <algorithm>

namespace glyphware {
namespace {

//---------------------------------------------------------------------------
// UAX #50 Vertical_Orientation (upright ranges)
//---------------------------------------------------------------------------
// Everything not covered here is Rotated. Sorted, searched by bisection.
struct Range { char32_t first; char32_t last; };

constexpr Range kUprightRanges[] = {
    {0x1100, 0x11FF},   // Hangul Jamo
    {0x2E80, 0x2EFF},   // CJK Radicals Supplement
    {0x2F00, 0x2FDF},   // Kangxi Radicals
    {0x2FF0, 0x2FFF},   // Ideographic Description Characters
    {0x3000, 0x303F},   // CJK Symbols and Punctuation
    {0x3040, 0x309F},   // Hiragana
    {0x30A0, 0x30FF},   // Katakana
    {0x3100, 0x312F},   // Bopomofo
    {0x3130, 0x318F},   // Hangul Compatibility Jamo
    {0x3190, 0x319F},   // Kanbun
    {0x31A0, 0x31BF},   // Bopomofo Extended
    {0x31C0, 0x31EF},   // CJK Strokes
    {0x31F0, 0x31FF},   // Katakana Phonetic Extensions
    {0x3200, 0x32FF},   // Enclosed CJK Letters and Months
    {0x3300, 0x33FF},   // CJK Compatibility
    {0x3400, 0x4DBF},   // CJK Unified Ideographs Extension A
    {0x4E00, 0x9FFF},   // CJK Unified Ideographs
    {0xA000, 0xA4CF},   // Yi
    {0xA960, 0xA97F},   // Hangul Jamo Extended-A
    {0xAC00, 0xD7FF},   // Hangul Syllables / Jamo Extended-B
    {0xF900, 0xFAFF},   // CJK Compatibility Ideographs
    {0xFE10, 0xFE1F},   // Vertical Forms
    {0xFE30, 0xFE4F},   // CJK Compatibility Forms
    {0xFE50, 0xFE6F},   // Small Form Variants
    {0xFF01, 0xFF60},   // Fullwidth Forms (halfwidth katakana FF61-FF9F rotates)
    {0xFFE0, 0xFFE6},   // Fullwidth signs
    {0x1B000, 0x1B16F}, // Kana Supplement / Extended
    {0x1F200, 0x1F2FF}, // Enclosed Ideographic Supplement
    {0x1F300, 0x1F5FF}, // Miscellaneous Symbols and Pictographs
    {0x1F600, 0x1F64F}, // Emoticons
    {0x1F680, 0x1F6FF}, // Transport and Map Symbols
    {0x1F900, 0x1F9FF}, // Supplemental Symbols and Pictographs
    {0x1FA70, 0x1FAFF}, // Symbols and Pictographs Extended-A
    {0x20000, 0x2FFFD}, // CJK Unified Ideographs Extension B and up
    {0x30000, 0x3FFFD}, // CJK Unified Ideographs Extension G and up
};

Face* resolveFace(char32_t cp, const std::vector<std::shared_ptr<Face>>& chain) {
    for (auto& f : chain) if (f && f->covers(cp)) return f.get();
    return chain.empty() ? nullptr : chain[0].get();
}

// A maximal byte range resolved to one face AND one orientation.
struct VerticalSpan {
    std::size_t byteStart;
    std::size_t byteLen;
    Face* face;
    bool upright;
};

std::vector<VerticalSpan> itemizeVertical(std::string_view text,
                                          const std::vector<std::shared_ptr<Face>>& chain,
                                          TextOrientation mode) {
    std::vector<VerticalSpan> spans;
    std::size_t i = 0;
    while (i < text.size()) {
        char32_t cp;
        int n = utf8::decodeAt(text, i, cp);
        Face* f = resolveFace(cp, chain);
        bool upright = mode == TextOrientation::Upright  ? true
                     : mode == TextOrientation::Sideways ? false
                     : charOrientation(cp) == CharOrientation::Upright;
        if (!spans.empty() && spans.back().face == f && spans.back().upright == upright)
            spans.back().byteLen += n;
        else
            spans.push_back({i, static_cast<std::size_t>(n), f, upright});
        i += n;
    }
    return spans;
}

} // namespace

CharOrientation charOrientation(char32_t cp) {
    if (cp < kUprightRanges[0].first) return CharOrientation::Rotated;

    const Range* begin = std::begin(kUprightRanges);
    const Range* end = std::end(kUprightRanges);
    // last range whose `first` is <= cp
    const Range* it = std::upper_bound(begin, end, cp,
                                       [](char32_t v, const Range& r) { return v < r.first; });
    if (it == begin) return CharOrientation::Rotated;
    --it;
    return (cp <= it->last) ? CharOrientation::Upright : CharOrientation::Rotated;
}

VerticalLineLayout layoutVerticalLine(std::string_view utf8,
                                      const std::vector<std::shared_ptr<Face>>& chain,
                                      int pixelSize,
                                      TextOrientation orientation) {
    VerticalLineLayout out;
    if (chain.empty() || utf8.empty() || pixelSize <= 0) return out;

    for (auto& f : chain) if (f) f->setPixelSize(pixelSize);
    const float size = static_cast<float>(pixelSize);

    float pen = 0.f;                    // v: advance along the column
    float minU = 0.f, maxU = 0.f;
    bool haveExtent = false;

    std::vector<ShapedGlyph> shaped;
    for (const VerticalSpan& span : itemizeVertical(utf8, chain, orientation)) {
        if (!span.face) continue;
        std::string_view spanText = utf8.substr(span.byteStart, span.byteLen);

        ShapeOptions opts;
        opts.guessSegmentProperties = true;
        opts.direction = span.upright ? Direction::TTB : Direction::LTR;
        shaped.clear();
        shapeRun(*span.face, spanText, opts, shaped);
        if (shaped.empty()) continue;

        // A sideways run keeps its Latin baseline, which after the 90 degree tip
        // runs along one side of the column. Shift it so the run's ascent and
        // descent straddle the column centre instead.
        float baselineU = 0.f, ascent = 0.f, descent = 0.f;
        if (!span.upright) {
            const LineMetrics lm = span.face->lineMetrics();
            const float upem = (lm.unitsPerEm > 0.f) ? lm.unitsPerEm : 1000.f;
            ascent = lm.ascenderUnits / upem * size;      // positive
            descent = -lm.descenderUnits / upem * size;   // positive
            baselineU = (descent - ascent) * 0.5f;
        }

        // Group by HarfBuzz cluster: that is the unit the typesetting layer moves.
        const std::size_t n = shaped.size();
        for (std::size_t i = 0; i < n;) {
            std::size_t j = i;
            while (j + 1 < n && shaped[j + 1].cluster == shaped[i].cluster) ++j;

            VerticalCluster vc;
            vc.glyphStart = static_cast<std::uint32_t>(out.glyphs.size());
            vc.byteStart = span.byteStart + shaped[i].cluster;
            vc.byteEnd = (j + 1 < n) ? span.byteStart + shaped[j + 1].cluster
                                     : span.byteStart + span.byteLen;
            vc.origin = pen;
            vc.upright = span.upright;
            {
                char32_t cp = 0;
                if (vc.byteStart < utf8.size()) utf8::decodeAt(utf8, vc.byteStart, cp);
                vc.lead = cp;
            }

            float clusterAdvance = 0.f;
            for (std::size_t k = i; k <= j; ++k) {
                const ShapedGlyph& g = shaped[k];
                VerticalGlyph vg;
                vg.face = span.face;
                vg.gid = g.gid;
                vg.cluster = static_cast<std::uint32_t>(span.byteStart + g.cluster);

                float adv;
                if (span.upright) {
                    // TTB: HarfBuzz already subtracts the vertical origin, so the
                    // glyph sits at the pen with its usual horizontal origin. Its
                    // y is up-positive while the column runs down, hence the sign.
                    vg.u = g.xOffset;
                    vg.v = pen - g.yOffset;
                    adv = -g.yAdvance;
                } else {
                    // Sideways: laid out horizontally, then tipped into the column.
                    //   local (lx, ly) -> column (u, v) = (-ly, lx)
                    const float ly = -g.yOffset;
                    vg.u = -ly + baselineU;
                    vg.v = pen + g.xOffset;
                    vg.rotation = kSidewaysRotation;
                    adv = g.xAdvance;
                }
                vg.advance = adv;
                clusterAdvance += adv;
                pen += adv;
                out.glyphs.push_back(vg);
            }

            vc.glyphCount = static_cast<std::uint32_t>(out.glyphs.size()) - vc.glyphStart;
            vc.advance = clusterAdvance;
            out.clusters.push_back(vc);
            i = j + 1;
        }

        if (span.upright) {
            // an upright run occupies the solid 1em column width
            minU = std::min(minU, -size * 0.5f);
            maxU = std::max(maxU, size * 0.5f);
        } else {
            minU = std::min(minU, baselineU - descent);
            maxU = std::max(maxU, baselineU + ascent);
        }
        haveExtent = true;
    }

    out.advance = pen;
    if (haveExtent) {
        out.extentLeft = minU;
        out.extentRight = maxU;
    }
    return out;
}

} // namespace glyphware
