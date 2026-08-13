// glyphware — block (rectangle) text layout: the break provider layoutLine
// deliberately does not have. Paragraph splitting, greedy word / per-character
// wrapping with 禁則, alignment, and the cluster `count` limit all live here so
// that every consumer (the 吉里吉里Z Layer draw path, the Elements/ThorVG text
// backend) wraps text the same way.
//
// Everything works on byte offsets into the caller's UTF-8; `PositionedGlyph`
// cluster values are byte offsets too, so the two agree without a conversion
// table.

#include "glyphware/Layout.h"
#include "Utf8.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>

namespace glyphware {
namespace {

using Range = std::pair<std::size_t, std::size_t>;   // [start, end) in bytes

//---------------------------------------------------------------------------
// character classes
//---------------------------------------------------------------------------

bool isSpaceCp(char32_t c) {
    return c == 0x20 || c == 0x09 || c == 0x3000;
}

// Characters that must stick to the preceding unit (never a break before, and
// never counted as an own base): ZWJ, variation selectors, emoji modifiers,
// combining marks, kana voicing marks, keycap.
bool isGlueCp(char32_t c) {
    return c == 0x200D || c == 0xFE0E || c == 0xFE0F
        || (c >= 0x1F3FB && c <= 0x1F3FF)
        || (c >= 0x0300 && c <= 0x036F)
        || c == 0x3099 || c == 0x309A || c == 0x20E3;
}

// Scripts that wrap per character (no word spacing): CJK ideographs, kana,
// hangul, fullwidth forms — plus emoji blocks (each emoji breaks on its own).
bool isCJKCp(char32_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF)      // radicals, kana, CJK, ext A ...
        || (c >= 0xA000 && c <= 0xA4CF)
        || (c >= 0xAC00 && c <= 0xD7AF)
        || (c >= 0xF900 && c <= 0xFAFF)
        || (c >= 0xFE30 && c <= 0xFE4F)
        || (c >= 0xFF00 && c <= 0xFF60)
        || (c >= 0xFFE0 && c <= 0xFFE6)
        || (c >= 0x20000 && c <= 0x3FFFF)
        || (c >= 0x1F000 && c <= 0x1FAFF)
        || (c >= 0x2600 && c <= 0x27BF);
}

// 行頭禁則: characters a line must not start with.
bool isNoStartCp(char32_t c) {
    switch (c) {
    case 0x3001: case 0x3002:                           // 、。
    case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: // ，．：；
    case 0xFF1F: case 0xFF01:                           // ？！
    case 0x30FB: case 0x30FC:                           // ・ー
    case 0x309B: case 0x309C: case 0x30FD: case 0x30FE: // ゛゜ヽヾ
    case 0x309D: case 0x309E: case 0x3005:              // ゝゞ々
    case 0x2019: case 0x201D:                           // ’”
    case 0xFF09: case 0x3015: case 0xFF3D: case 0xFF5D: // ）〕］｝
    case 0x3009: case 0x300B: case 0x300D: case 0x300F: // 〉》」』
    case 0x3011: case 0x3017: case 0x301F:              // 】〙〟
    case 0x2026: case 0x2025:                           // …‥
    case 0xFF61: case 0xFF63: case 0xFF64: case 0xFF65: // ｡｣､･ (halfwidth)
    case U')': case U']': case U'}': case U',': case U'.':
    case U':': case U';': case U'!': case U'?': case 0x00BB:
        return true;
    default:
        break;
    }
    // small kana
    switch (c) {
    case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049:
    case 0x3063: case 0x3083: case 0x3085: case 0x3087: case 0x308E:
    case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9:
    case 0x30C3: case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE:
    case 0x30F5: case 0x30F6:
        return true;
    default:
        return false;
    }
}

// 行末禁則: characters a line must not end with (opening brackets/quotes).
bool isNoEndCp(char32_t c) {
    switch (c) {
    case 0xFF08: case 0x3014: case 0xFF3B: case 0xFF5B: // （〔［｛
    case 0x3008: case 0x300A: case 0x300C: case 0x300E: // 〈《「『
    case 0x3010: case 0x3016: case 0x301D:              // 【〖〝
    case 0x2018: case 0x201C:                           // ‘“
    case 0xFF62:                                        // ｢ (halfwidth)
    case U'(': case U'[': case U'{': case 0x00AB:
        return true;
    default:
        return false;
    }
}

//---------------------------------------------------------------------------
// segmentation
//---------------------------------------------------------------------------

// One atom = a base character plus its glue characters (a ZWJ also glues the
// following base character, so an emoji ZWJ sequence stays one atom).
struct Atom {
    std::size_t start = 0, end = 0;
    char32_t cp = 0;   // base (first) codepoint, for classification
};

void splitAtoms(std::string_view s, std::size_t start, std::size_t end,
                std::vector<Atom>& atoms) {
    atoms.clear();
    std::size_t i = start;
    while (i < end) {
        Atom a;
        a.start = i;
        i += static_cast<std::size_t>(utf8::decodeAt(s, i, a.cp));
        bool glueNext = false;
        for (;;) {
            if (i >= end) break;
            char32_t c2;
            const std::size_t j = i + static_cast<std::size_t>(utf8::decodeAt(s, i, c2));
            if (glueNext) { i = j; glueNext = false; continue; }
            if (isGlueCp(c2)) {
                i = j;
                if (c2 == 0x200D) glueNext = true;
                continue;
            }
            break;
        }
        a.end = i;
        atoms.push_back(a);
    }
}

// One unit = an unbreakable wrap segment: a word (space-separated scripts) or
// a CJK character, extended by kinsoku merges; trailing spaces attach to it.
struct Unit {
    std::size_t start = 0, end = 0;
    char32_t lastCp = 0;        // last non-space base codepoint
    bool lastIsCJK = false;
    bool hasTrailingSpace = false;
};

void splitUnits(std::string_view s, std::size_t start, std::size_t end,
                std::vector<Unit>& units) {
    units.clear();
    std::vector<Atom> atoms;
    splitAtoms(s, start, end, atoms);
    for (const Atom& a : atoms) {
        if (isSpaceCp(a.cp)) {
            if (units.empty()) {
                // paragraph-leading spaces: keep as their own unit (indentation)
                Unit u; u.start = a.start; u.end = a.end;
                u.lastCp = a.cp; u.lastIsCJK = false; u.hasTrailingSpace = true;
                units.push_back(u);
            } else {
                units.back().end = a.end;
                units.back().hasTrailingSpace = true;
            }
            continue;
        }
        const bool cjk = isCJKCp(a.cp);
        bool merge = false;
        if (!units.empty() && !units.back().hasTrailingSpace) {
            if (isNoStartCp(a.cp)) merge = true;                   // 行頭禁則
            else if (isNoEndCp(units.back().lastCp)) merge = true;  // 行末禁則
            else if (!cjk && !units.back().lastIsCJK) merge = true; // word run
        }
        if (merge) {
            units.back().end = a.end;
            units.back().lastCp = a.cp;
            units.back().lastIsCJK = cjk;
        } else {
            Unit u; u.start = a.start; u.end = a.end;
            u.lastCp = a.cp; u.lastIsCJK = cjk;
            units.push_back(u);
        }
    }
}

// paragraphs = text split at \n / \r\n / \r (ranges exclude the newline)
void splitParagraphs(std::string_view s, std::vector<Range>& paras) {
    paras.clear();
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            paras.emplace_back(start, i);
            start = i + 1;
        } else if (s[i] == '\r') {
            paras.emplace_back(start, i);
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
            start = i + 1;
        }
    }
    paras.emplace_back(start, s.size());
}

std::size_t trimTrailingSpaces(std::string_view s, std::size_t start, std::size_t end) {
    while (end > start) {
        const std::size_t p = utf8::prevStart(s, start, end);
        char32_t cp;
        utf8::decodeAt(s, p, cp);
        if (!isSpaceCp(cp)) break;
        end = p;
    }
    return end;
}

//---------------------------------------------------------------------------
// wrapping
//---------------------------------------------------------------------------

// Everything layoutLine needs, carried through the wrap so the measuring calls
// stay short.
struct Ctx {
    std::string_view text;
    BaseDirection base = BaseDirection::Auto;
    const std::vector<std::shared_ptr<Face>>* chain = nullptr;
    int pixelSize = 0;

    LineLayout layout(std::size_t start, std::size_t end) const {
        return layoutLine(text.substr(start, end - start), base, *chain, pixelSize);
    }
    float measure(std::size_t start, std::size_t end) const {
        end = trimTrailingSpaces(text, start, end);
        if (start >= end) return 0.f;
        return layout(start, end).width;
    }
};

// Greedy wrap of one paragraph into lines fitting areaW. Always emits at least
// one line (possibly empty); an oversized single unit falls back to per-atom
// splitting so progress is guaranteed.
void wrapParagraph(const Ctx& cx, std::size_t paraStart, std::size_t paraEnd,
                   float areaW, std::vector<Range>& lines) {
    if (paraStart >= paraEnd) {
        lines.emplace_back(paraStart, paraStart);
        return;
    }
    if (areaW <= 0.f) {   // no wrap width given: break only at newlines
        lines.emplace_back(paraStart, paraEnd);
        return;
    }
    std::vector<Unit> units;
    splitUnits(cx.text, paraStart, paraEnd, units);

    std::size_t lineStart = paraStart;
    std::size_t accepted = paraStart;
    std::vector<Atom> atoms;
    for (const Unit& u : units) {
        const float w = cx.measure(lineStart, u.end);
        if (w <= areaW || accepted == lineStart) {
            if (w <= areaW) { accepted = u.end; continue; }
            // single unit wider than the area: split it per atom
            splitAtoms(cx.text, lineStart, u.end, atoms);
            std::size_t cs = lineStart, ce = lineStart;
            for (const Atom& a : atoms) {
                const float w2 = cx.measure(cs, a.end);
                if (w2 <= areaW || ce == cs) ce = a.end;
                else { lines.emplace_back(cs, ce); cs = a.start; ce = a.end; }
            }
            lineStart = cs;
            accepted = ce;
        } else {
            lines.emplace_back(lineStart, accepted);
            lineStart = u.start;
            accepted = u.end;
            const float w2 = cx.measure(lineStart, u.end);
            if (w2 > areaW) {
                // the unit alone overflows on its fresh line: per-atom split
                splitAtoms(cx.text, lineStart, u.end, atoms);
                std::size_t cs = lineStart, ce = lineStart;
                for (const Atom& a : atoms) {
                    const float w3 = cx.measure(cs, a.end);
                    if (w3 <= areaW || ce == cs) ce = a.end;
                    else { lines.emplace_back(cs, ce); cs = a.start; ce = a.end; }
                }
                lineStart = cs;
                accepted = ce;
            }
        }
    }
    lines.emplace_back(lineStart, accepted);
}

//---------------------------------------------------------------------------
// clusters
//---------------------------------------------------------------------------

// Distinct cluster values of a line, ascending = logical order.
void collectClusters(const LineLayout& ll, std::vector<std::uint32_t>& sorted) {
    sorted.clear();
    sorted.reserve(ll.glyphs.size());
    for (const PositionedGlyph& g : ll.glyphs) sorted.push_back(g.cluster);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
}

} // namespace

//---------------------------------------------------------------------------
int lineClusterCount(const LineLayout& line) {
    std::vector<std::uint32_t> sorted;
    collectClusters(line, sorted);
    return static_cast<int>(sorted.size());
}
//---------------------------------------------------------------------------
int limitClusters(LineLayout& line, int count) {
    std::vector<std::uint32_t> sorted;
    collectClusters(line, sorted);
    const int total = static_cast<int>(sorted.size());
    if (count < 0 || count >= total) return total;
    if (count == 0) {
        line.glyphs.clear();
        return 0;
    }
    const std::uint32_t maxCluster = sorted[static_cast<std::size_t>(count) - 1];
    line.glyphs.erase(
        std::remove_if(line.glyphs.begin(), line.glyphs.end(),
                       [maxCluster](const PositionedGlyph& g) {
                           return g.cluster > maxCluster;
                       }),
        line.glyphs.end());
    return count;
}
//---------------------------------------------------------------------------
int countClusters(std::string_view utf8, BaseDirection base,
                  const std::vector<std::shared_ptr<Face>>& chain, int pixelSize) {
    if (chain.empty()) return 0;
    Ctx cx{utf8, base, &chain, pixelSize};
    std::vector<Range> paras;
    splitParagraphs(utf8, paras);
    int total = 0;
    for (const Range& p : paras) {
        if (p.first >= p.second) continue;
        total += lineClusterCount(cx.layout(p.first, p.second));
    }
    return total;
}
//---------------------------------------------------------------------------
BlockLayout layoutBlock(std::string_view utf8, BaseDirection base,
                        const std::vector<std::shared_ptr<Face>>& chain,
                        int pixelSize, const BlockOptions& opts) {
    BlockLayout out;
    if (chain.empty()) return out;

    for (auto& f : chain) if (f) f->setPixelSize(pixelSize);
    const LineMetrics lm = chain[0]->lineMetrics();
    // Line geometry is snapped to whole pixels: consumers blit glyph bitmaps at
    // integer positions, so a fractional pitch would only add jitter.
    const int ascent = static_cast<int>(std::lround(lm.ascent));
    const int descent = static_cast<int>(std::lround(lm.descent));
    const int lineExtent = ascent + descent;
    int lineH = lineExtent + static_cast<int>(std::lround(lm.lineGap))
              + static_cast<int>(std::lround(opts.lineSpacing));
    if (lineH < 1) lineH = 1;
    out.ascent = static_cast<float>(ascent);
    out.descent = static_cast<float>(descent);
    out.lineHeight = static_cast<float>(lineH);

    if (opts.count == 0) return out;   // nothing revealed yet

    Ctx cx{utf8, base, &chain, pixelSize};

    // Wrap the FULL text first so a count-limited (typewriter) reveal never
    // changes the line breaks.
    std::vector<Range> paras, lines;
    splitParagraphs(utf8, paras);
    for (const Range& p : paras)
        wrapParagraph(cx, p.first, p.second, opts.width, lines);

    const bool boundH = opts.height > 0.f;
    const int areaW = static_cast<int>(opts.width);
    int remaining = opts.count < 0 ? INT_MAX : opts.count;
    int lineIndex = 0;

    for (const Range& lr : lines) {
        const int top = lineIndex * lineH;
        if (boundH && static_cast<float>(top + lineExtent) > opts.height) break;
        if (remaining <= 0) break;

        BlockLine bl;
        bl.byteStart = lr.first;
        bl.byteEnd = trimTrailingSpaces(utf8, lr.first, lr.second);
        bl.y = static_cast<float>(top + ascent);

        if (bl.byteStart < bl.byteEnd) {
            bl.layout = cx.layout(bl.byteStart, bl.byteEnd);
            bl.totalClusters = lineClusterCount(bl.layout);
            const int allowed = bl.totalClusters < remaining ? bl.totalClusters : remaining;
            bl.clusters = limitClusters(bl.layout, allowed);
            remaining -= bl.clusters;
            out.drawnClusters += bl.clusters;
            out.totalClusters += bl.totalClusters;

            // Alignment uses the full line width, not the revealed part.
            const int lw = static_cast<int>(std::lround(bl.layout.width));
            if (opts.align == Align::Center) bl.x = static_cast<float>((areaW - lw) / 2);
            else if (opts.align == Align::Right) bl.x = static_cast<float>(areaW - lw);
            if (bl.layout.width > out.width) out.width = bl.layout.width;
        }

        out.lines.push_back(std::move(bl));
        ++lineIndex;
    }

    out.lineCount = lineIndex;
    out.height = lineIndex > 0
        ? static_cast<float>((lineIndex - 1) * lineH + lineExtent) : 0.f;
    return out;
}

} // namespace glyphware
