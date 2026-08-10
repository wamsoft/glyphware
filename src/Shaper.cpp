#include "glyphware/Shaper.h"

#include <hb.h>

namespace glyphware {

void shapeRun(Face& face, std::string_view utf8, const ShapeOptions& opts,
              std::vector<ShapedGlyph>& out) {
    if (utf8.empty()) return;
    hb_font_t* font = face.hb();
    if (!font) return;

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, utf8.data(), static_cast<int>(utf8.size()), 0,
                       static_cast<int>(utf8.size()));

    if (opts.guessSegmentProperties) {
        hb_buffer_guess_segment_properties(buf);
    } else {
        hb_buffer_set_direction(buf, opts.direction == Direction::RTL
                                          ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    }
    if (!opts.script.empty())
        hb_buffer_set_script(buf, hb_script_from_string(opts.script.c_str(), -1));
    if (!opts.language.empty())
        hb_buffer_set_language(buf, hb_language_from_string(opts.language.c_str(), -1));

    hb_shape(font, buf, nullptr, 0);

    unsigned int n = 0;
    const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
    // Fixed-strike (bitmap-only) fonts shape at the strike ppem; scale the
    // output to the requested pixel size so advances match glyphBitmap().
    const float strikeScale = face.fixedStrikeScale();
    out.reserve(out.size() + n);
    for (unsigned int i = 0; i < n; ++i) {
        ShapedGlyph g;
        g.gid = info[i].codepoint;   // after shaping this is the glyph id
        g.cluster = info[i].cluster;
        g.xAdvance = pos[i].x_advance / 64.0f * strikeScale;
        g.yAdvance = pos[i].y_advance / 64.0f * strikeScale;
        g.xOffset = pos[i].x_offset / 64.0f * strikeScale;
        g.yOffset = pos[i].y_offset / 64.0f * strikeScale;
        out.push_back(g);
    }
    hb_buffer_destroy(buf);
}

} // namespace glyphware
