// glyphware — BiDi (Unicode Bidirectional Algorithm) analysis.
//
// Turns a logical-order paragraph into VISUAL-order runs so a caller can shape
// each run and place them left to right. This is a deliberately narrow seam:
// the backend (currently SheenBidi, see Bidi_sheenbidi.cpp) can be swapped
// without touching callers, and it is independent of line/word breaking — a
// separate concern added later without changing this.
#ifndef GLYPHWARE_BIDI_H
#define GLYPHWARE_BIDI_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace glyphware {

enum class BaseDirection { Auto, LTR, RTL };

// A maximal run of one embedding level. `offset`/`length` are byte offsets into
// the source UTF-8 (so a run can be fed directly to shapeRun on a substring).
struct BidiRun {
    std::size_t offset;
    std::size_t length;
    int level;      // Unicode embedding level
    bool rtl;       // level is odd
};

struct BidiResult {
    int paragraphLevel = 0;              // resolved base level: 0 = LTR, 1 = RTL
    std::vector<BidiRun> runs;           // VISUAL (left-to-right) order
    std::vector<std::uint8_t> levels;    // per source byte (UTF-8 code unit)
};

// Analyze one paragraph of UTF-8 into visual-order runs. `base` = Auto resolves
// the base direction from the first strong character (UBA P2/P3), LTR fallback.
BidiResult bidiAnalyze(std::string_view utf8, BaseDirection base = BaseDirection::Auto);

} // namespace glyphware

#endif // GLYPHWARE_BIDI_H
