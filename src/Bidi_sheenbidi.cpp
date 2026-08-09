// glyphware — BiDi backend: SheenBidi (Apache-2.0).
//
// This file is the swap point for the BiDi backend. Replacing SheenBidi with
// another UBA implementation (e.g. ICU ubidi) means reimplementing only
// bidiAnalyze() here; Bidi.h and all callers stay unchanged.
#include "glyphware/Bidi.h"

#include <SheenBidi/SheenBidi.h>

namespace glyphware {

BidiResult bidiAnalyze(std::string_view utf8, BaseDirection base) {
    BidiResult res;
    if (utf8.empty()) return res;

    SBCodepointSequence seq;
    seq.stringEncoding = SBStringEncodingUTF8;
    seq.stringBuffer = const_cast<char*>(utf8.data());
    seq.stringLength = static_cast<SBUInteger>(utf8.size());

    SBAlgorithmRef algorithm = SBAlgorithmCreate(&seq);
    if (!algorithm) return res;

    SBLevel baseLevel = SBLevelDefaultLTR;
    if (base == BaseDirection::LTR) baseLevel = 0;
    else if (base == BaseDirection::RTL) baseLevel = 1;
    // Auto -> SBLevelDefaultLTR (resolve from first strong char, LTR fallback)

    SBParagraphRef paragraph =
        SBAlgorithmCreateParagraph(algorithm, 0, static_cast<SBUInteger>(utf8.size()), baseLevel);
    if (paragraph) {
        res.paragraphLevel = SBParagraphGetBaseLevel(paragraph) & 1;

        SBUInteger paraLen = SBParagraphGetLength(paragraph);
        const SBLevel* levels = SBParagraphGetLevelsPtr(paragraph);
        if (levels) res.levels.assign(levels, levels + paraLen);

        SBLineRef line = SBParagraphCreateLine(paragraph, 0, paraLen);
        if (line) {
            SBUInteger runCount = SBLineGetRunCount(line);
            const SBRun* runs = SBLineGetRunsPtr(line);
            res.runs.reserve(runCount);
            for (SBUInteger i = 0; i < runCount; ++i) {
                BidiRun r;
                r.offset = static_cast<std::size_t>(runs[i].offset);
                r.length = static_cast<std::size_t>(runs[i].length);
                r.level = runs[i].level;
                r.rtl = (runs[i].level & 1) != 0;
                res.runs.push_back(r);
            }
            SBLineRelease(line);
        }
        SBParagraphRelease(paragraph);
    }
    SBAlgorithmRelease(algorithm);
    return res;
}

} // namespace glyphware
