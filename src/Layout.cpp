#include "glyphware/Layout.h"
#include "glyphware/Shaper.h"
#include "Utf8.h"

#include <algorithm>

namespace glyphware {
namespace {

// A maximal span of `text` (byte range) resolved to one face.
struct FaceSpan { std::size_t byteStart; std::size_t byteLen; Face* face; };

Face* resolveFace(char32_t cp, const std::vector<std::shared_ptr<Face>>& chain) {
    for (auto& f : chain) if (f && f->covers(cp)) return f.get();
    return chain.empty() ? nullptr : chain[0].get();
}

// Itemize `run` (logical order) into face spans.
std::vector<FaceSpan> itemize(std::string_view run,
                              const std::vector<std::shared_ptr<Face>>& chain) {
    std::vector<FaceSpan> spans;
    std::size_t i = 0;
    while (i < run.size()) {
        char32_t cp;
        int n = utf8::decodeAt(run, i, cp);
        Face* f = resolveFace(cp, chain);
        if (!spans.empty() && spans.back().face == f)
            spans.back().byteLen += n;
        else
            spans.push_back({i, static_cast<std::size_t>(n), f});
        i += n;
    }
    return spans;
}

} // namespace

LineLayout layoutLine(std::string_view utf8, BaseDirection base,
                      const std::vector<std::shared_ptr<Face>>& chain, int pixelSize) {
    LineLayout out;
    if (chain.empty() || utf8.empty()) return out;

    for (auto& f : chain) if (f) f->setPixelSize(pixelSize);
    LineMetrics lm = chain[0]->lineMetrics();
    out.ascent = lm.ascent;
    out.descent = lm.descent;

    BidiResult bidi = bidiAnalyze(utf8, base);

    float penX = 0.f;
    std::vector<ShapedGlyph> shaped;
    for (const BidiRun& run : bidi.runs) {
        std::string_view runText = utf8.substr(run.offset, run.length);
        std::vector<FaceSpan> spans = itemize(runText, chain);
        // Within an RTL run the visual order of the face spans is reversed.
        if (run.rtl) std::reverse(spans.begin(), spans.end());

        for (const FaceSpan& span : spans) {
            if (!span.face) continue;
            std::string_view spanText = runText.substr(span.byteStart, span.byteLen);
            ShapeOptions opts;
            // Guess script/language from content; direction matches this run.
            opts.guessSegmentProperties = true;
            shaped.clear();
            shapeRun(*span.face, spanText, opts, shaped);
            for (const ShapedGlyph& g : shaped) {
                PositionedGlyph pg;
                pg.face = span.face;
                pg.gid = g.gid;
                pg.x = penX;
                pg.y = 0.f;
                pg.xOffset = g.xOffset;
                pg.yOffset = g.yOffset;
                pg.advance = g.xAdvance;
                pg.cluster = static_cast<std::uint32_t>(run.offset + span.byteStart) + g.cluster;
                pg.rtl = run.rtl;
                out.glyphs.push_back(pg);
                penX += g.xAdvance;
            }
        }
    }
    out.width = penX;
    return out;
}

} // namespace glyphware
