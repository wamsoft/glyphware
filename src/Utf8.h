// glyphware — internal UTF-8 scanning helpers.
//
// Not part of the public API: the public surface takes/returns byte offsets
// into the caller's UTF-8 and never needs these. Shared by Layout.cpp (face
// itemization) and Block.cpp (line breaking).
#ifndef GLYPHWARE_SRC_UTF8_H
#define GLYPHWARE_SRC_UTF8_H

#include <cstddef>
#include <string_view>

namespace glyphware {
namespace utf8 {

// Decode one codepoint at byte `i`; returns the byte length consumed (>= 1).
// Invalid / truncated sequences yield the lead byte itself and consume 1 byte,
// so scanning always makes progress on malformed input.
inline int decodeAt(std::string_view s, std::size_t i, char32_t& cp) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { cp = c; return 1; }
    int extra; char32_t v;
    if ((c >> 5) == 0x6) { v = c & 0x1F; extra = 1; }
    else if ((c >> 4) == 0xE) { v = c & 0x0F; extra = 2; }
    else if ((c >> 3) == 0x1E) { v = c & 0x07; extra = 3; }
    else { cp = c; return 1; }  // invalid lead; consume one byte
    if (i + extra >= s.size()) { cp = c; return 1; }
    for (int k = 0; k < extra; ++k)
        v = (v << 6) | (static_cast<unsigned char>(s[i + 1 + k]) & 0x3F);
    cp = v;
    return extra + 1;
}

// Byte offset where the codepoint ending at `end` starts (>= start). Walks back
// over continuation bytes; stops at `start` so it never runs off a substring.
inline std::size_t prevStart(std::string_view s, std::size_t start, std::size_t end) {
    if (end <= start) return end;
    std::size_t i = end - 1;
    while (i > start && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

} // namespace utf8
} // namespace glyphware

#endif // GLYPHWARE_SRC_UTF8_H
