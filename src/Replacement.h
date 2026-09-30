// glyphware — drawing characters that no face in the chain covers
//
// A character no chain face has is drawn as U+FFFD REPLACEMENT CHARACTER, taken
// from the first chain face that has it; failing that '?', and only if neither
// exists the primary face's .notdef (the historical behaviour). Default-ignorable
// characters (controls, joiners, variation selectors, ...) produce no glyph.
//
// Each replacement glyph keeps the byte offset of the character it stands for as
// its cluster, so line breaking and cluster counting see one unit per original
// character exactly as before; the source text itself is never rewritten.
#pragma once

#include "glyphware/Face.h"
#include "glyphware/Shaper.h"
#include "glyphware/Layout.h"   // isDefaultIgnorable

#include <memory>
#include <string_view>
#include <vector>

namespace glyphware {
namespace detail {

// Shape `spanText` (a run no chain face covers) as replacement characters.
// `faceOut` receives the face the glyphs belong to. Clusters are byte offsets
// into `spanText`, like shapeRun's.
void shapeReplacement(std::string_view spanText, const ShapeOptions& opts,
                      const std::vector<std::shared_ptr<Face>>& chain,
                      Face*& faceOut, std::vector<ShapedGlyph>& out);

} // namespace detail
} // namespace glyphware
