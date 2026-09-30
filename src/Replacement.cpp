#include "Replacement.h"
#include "Utf8.h"

#include <string>

namespace glyphware {

bool isDefaultIgnorable(char32_t cp) {
    if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return true;        // C0 / DEL / C1
    if (cp == 0x00AD || cp == 0x034F || cp == 0x061C) return true;    // SHY / CGJ / ALM
    if (cp >= 0x115F && cp <= 0x1160) return true;                    // Hangul fillers
    if (cp >= 0x17B4 && cp <= 0x17B5) return true;
    if (cp >= 0x180B && cp <= 0x180F) return true;                    // Mongolian FVS
    if (cp >= 0x200B && cp <= 0x200F) return true;                    // ZWSP / ZWNJ / ZWJ / marks
    if (cp >= 0x2028 && cp <= 0x202E) return true;                    // separators / embeddings
    if (cp >= 0x2060 && cp <= 0x206F) return true;                    // word joiner / invisibles
    if (cp == 0x3164 || cp == 0xFEFF || cp == 0xFFA0) return true;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;                    // variation selectors
    if (cp >= 0xFFF0 && cp <= 0xFFF8) return true;
    if (cp >= 0x1BCA0 && cp <= 0x1BCA3) return true;
    if (cp >= 0x1D173 && cp <= 0x1D17A) return true;
    if (cp >= 0xE0000 && cp <= 0xE0FFF) return true;                  // tags / VS supplement
    return false;
}

namespace detail {
namespace {

void appendUtf8(std::string& s, char32_t cp) {
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// First chain face covering `cp`, or nullptr.
Face* faceCovering(const std::vector<std::shared_ptr<Face>>& chain, char32_t cp) {
    for (auto& f : chain) if (f && f->covers(cp)) return f.get();
    return nullptr;
}

} // namespace

void shapeReplacement(std::string_view spanText, const ShapeOptions& opts,
                      const std::vector<std::shared_ptr<Face>>& chain,
                      Face*& faceOut, std::vector<ShapedGlyph>& out) {
    out.clear();
    faceOut = chain.empty() ? nullptr : chain[0].get();
    if (!faceOut) return;

    char32_t repl = 0xFFFD;
    Face* face = faceCovering(chain, repl);
    if (!face) { repl = U'?'; face = faceCovering(chain, repl); }
    if (!face) {
        // Nothing to substitute with: fall back to the primary face's .notdef.
        shapeRun(*faceOut, spanText, opts, out);
        return;
    }
    faceOut = face;

    // One replacement character per visible source character; remember where
    // each came from so the clusters can be mapped back.
    std::string text;
    std::vector<std::uint32_t> origin;
    std::size_t i = 0;
    while (i < spanText.size()) {
        char32_t cp;
        int n = utf8::decodeAt(spanText, i, cp);
        if (!isDefaultIgnorable(cp)) {
            origin.push_back(static_cast<std::uint32_t>(i));
            appendUtf8(text, repl);
        }
        i += n;
    }
    if (text.empty()) return;

    const std::size_t replLen = text.size() / origin.size();
    shapeRun(*face, text, opts, out);
    for (ShapedGlyph& g : out) {
        std::size_t k = replLen ? g.cluster / replLen : 0;
        if (k >= origin.size()) k = origin.size() - 1;
        g.cluster = origin[k];
    }
}

} // namespace detail
} // namespace glyphware
