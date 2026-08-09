#include "glyphware/Face.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H
#include FT_MULTIPLE_MASTERS_H
#include FT_SYNTHESIS_H

#include <hb.h>
#include <hb-ft.h>

#include <algorithm>
#include <cstring>

namespace glyphware {
namespace {

// Append one Unicode codepoint as UTF-8.
void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decode a raw SFNT name string to UTF-8. Windows (platform 3) and Unicode
// (platform 0) name records are UTF-16BE; Mac Roman (platform 1) is treated as
// Latin-1 (adequate for ASCII family names).
std::string decodeSfntName(const FT_SfntName& n) {
    std::string out;
    const bool utf16 = (n.platform_id == TT_PLATFORM_MICROSOFT ||
                        n.platform_id == TT_PLATFORM_APPLE_UNICODE);
    if (utf16) {
        for (FT_UInt i = 0; i + 1 < n.string_len; i += 2) {
            char32_t u = (static_cast<unsigned char>(n.string[i]) << 8) |
                          static_cast<unsigned char>(n.string[i + 1]);
            if (u >= 0xD800 && u <= 0xDBFF && i + 3 < n.string_len) {
                char32_t lo = (static_cast<unsigned char>(n.string[i + 2]) << 8) |
                               static_cast<unsigned char>(n.string[i + 3]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            appendUtf8(out, u);
        }
    } else {
        for (FT_UInt i = 0; i < n.string_len; ++i)
            appendUtf8(out, static_cast<unsigned char>(n.string[i]));
    }
    return out;
}

// Pick the best-matching name string for a given name id, preferring
// Windows/English, then Unicode, then anything.
std::string pickName(FT_Face face, FT_UShort nameId) {
    FT_UInt count = FT_Get_Sfnt_Name_Count(face);
    std::string best;
    int bestScore = -1;
    for (FT_UInt i = 0; i < count; ++i) {
        FT_SfntName n;
        if (FT_Get_Sfnt_Name(face, i, &n) != 0) continue;
        if (n.name_id != nameId) continue;
        int score = 0;
        if (n.platform_id == TT_PLATFORM_MICROSOFT) score += 2;
        if (n.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES) score += 1;
        if (score > bestScore) {
            std::string s = decodeSfntName(n);
            if (!s.empty()) { best = std::move(s); bestScore = score; }
        }
    }
    return best;
}

Weight clampWeight(int usWeight) {
    if (usWeight <= 0) return Weight::Regular;
    if (usWeight < 100) usWeight *= 100;  // some fonts store 1..9
    if (usWeight > 1000) usWeight = 1000;
    return static_cast<Weight>(usWeight);
}

} // namespace

std::shared_ptr<Face> Face::open(std::shared_ptr<FontBlob> blob, std::string key, int index) {
    if (!blob || blob->size() == 0) return nullptr;
    auto lib = Library::instance();
    std::shared_ptr<Face> face(new Face());
    face->lib_ = lib;
    face->blob_ = std::move(blob);
    face->desc_.key = std::move(key);
    face->desc_.faceIndex = index;

    LibraryLock lock(*lib);
    FT_Error err = FT_New_Memory_Face(lib->ft(), face->blob_->data(),
                                      static_cast<FT_Long>(face->blob_->size()),
                                      index, &face->face_);
    if (err != 0 || face->face_ == nullptr) return nullptr;

    // Prefer a Unicode charmap for codepoint lookups.
    FT_Select_Charmap(face->face_, FT_ENCODING_UNICODE);

    face->resolveMetadata();
    return face;
}

Face::~Face() {
    if (hb_) hb_font_destroy(hb_);
    if (face_) {
        LibraryLock lock(*lib_);
        FT_Done_Face(face_);
    }
}

void Face::resolveMetadata() {
    FontDescriptor& d = desc_;
    FT_Face f = face_;

    if (f->family_name) d.family = f->family_name;
    if (f->style_name) d.subfamily = f->style_name;

    // name table (richer / localized-neutral names)
    std::string typo = pickName(f, TT_NAME_ID_TYPOGRAPHIC_FAMILY);
    if (!typo.empty()) d.typographicFamily = typo;
    std::string fam = pickName(f, TT_NAME_ID_FONT_FAMILY);
    if (!fam.empty()) d.family = fam;
    std::string sub = pickName(f, TT_NAME_ID_FONT_SUBFAMILY);
    if (!sub.empty()) d.subfamily = sub;
    d.fullName = pickName(f, TT_NAME_ID_FULL_NAME);
    d.postScriptName = pickName(f, TT_NAME_ID_PS_NAME);
    if (!d.typographicFamily.empty()) d.family = d.typographicFamily;

    // OS/2 (weight / width / selection)
    auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(f, FT_SFNT_OS2));
    if (os2 && os2->version != 0xFFFF) {
        d.weight = clampWeight(os2->usWeightClass);
        int w = os2->usWidthClass;
        if (w >= 1 && w <= 9) d.width = static_cast<Width>(w);
        if (os2->fsSelection & 0x01) d.slant = Slant::Italic;
        if (os2->fsSelection & 0x200) d.slant = Slant::Oblique;
        if (os2->fsSelection & 0x20) d.bold = true;
    }
    // head macStyle as fallback
    auto* head = static_cast<TT_Header*>(FT_Get_Sfnt_Table(f, FT_SFNT_HEAD));
    if (head) {
        if ((head->Mac_Style & 0x01) && !d.bold) d.bold = true;
        if ((head->Mac_Style & 0x02) && d.slant == Slant::Normal) d.slant = Slant::Italic;
    }
    if (d.slant == Slant::Normal && (f->style_flags & FT_STYLE_FLAG_ITALIC))
        d.slant = Slant::Italic;
    if (!d.bold && (f->style_flags & FT_STYLE_FLAG_BOLD)) d.bold = true;

    d.color = FT_HAS_COLOR(f) != 0;
    d.monospace = FT_IS_FIXED_WIDTH(f) != 0;
    d.scalable = FT_IS_SCALABLE(f) != 0;

    // variable-font axes
    if (FT_HAS_MULTIPLE_MASTERS(f)) {
        FT_MM_Var* mm = nullptr;
        if (FT_Get_MM_Var(f, &mm) == 0 && mm) {
            for (FT_UInt i = 0; i < mm->num_axis; ++i) {
                VarAxis ax;
                ax.tag = static_cast<std::uint32_t>(mm->axis[i].tag);
                ax.minValue = mm->axis[i].minimum / 65536.0f;
                ax.defaultValue = mm->axis[i].def / 65536.0f;
                ax.maxValue = mm->axis[i].maximum / 65536.0f;
                if (mm->axis[i].name) ax.name = reinterpret_cast<const char*>(mm->axis[i].name);
                d.axes.push_back(std::move(ax));
            }
            FT_Done_MM_Var(lib_->ft(), mm);
        }
    }

    // cmap coverage as compact ranges
    if (f->charmap && f->charmap->encoding == FT_ENCODING_UNICODE) {
        FT_UInt gid = 0;
        FT_ULong ch = FT_Get_First_Char(f, &gid);
        char32_t rlo = 0, rhi = 0;
        bool have = false;
        while (gid != 0) {
            char32_t c = static_cast<char32_t>(ch);
            if (!have) { rlo = rhi = c; have = true; }
            else if (c == rhi + 1) { rhi = c; }
            else { d.ranges.push_back({rlo, rhi}); rlo = rhi = c; }
            ch = FT_Get_Next_Char(f, ch, &gid);
        }
        if (have) d.ranges.push_back({rlo, rhi});
    }

    d.metadataResolved = true;
}

hb_font_t* Face::hb() {
    if (!hb_) {
        hb_ = hb_ft_font_create_referenced(face_);
    }
    return hb_;
}

bool Face::setPixelSize(int pixels) {
    if (pixels <= 0) return false;
    LibraryLock lock(*lib_);
    if (FT_Set_Pixel_Sizes(face_, 0, static_cast<FT_UInt>(pixels)) != 0) return false;
    pixelSize_ = pixels;
    if (hb_) hb_ft_font_changed(hb_);
    return true;
}

GlyphId Face::glyphIndex(char32_t cp) const {
    return FT_Get_Char_Index(face_, static_cast<FT_ULong>(cp));
}

bool Face::glyphMetrics(GlyphId gid, GlyphMetrics& out) const {
    LibraryLock lock(*lib_);
    if (FT_Load_Glyph(face_, gid, FT_LOAD_DEFAULT) != 0) return false;
    const FT_Glyph_Metrics& m = face_->glyph->metrics;
    out.advanceX = face_->glyph->advance.x / 64.0f;
    out.advanceY = face_->glyph->advance.y / 64.0f;
    out.bearingX = m.horiBearingX / 64.0f;
    out.bearingY = m.horiBearingY / 64.0f;
    out.width = m.width / 64.0f;
    out.height = m.height / 64.0f;
    return true;
}

LineMetrics Face::lineMetrics() const {
    LineMetrics lm;
    lm.unitsPerEm = static_cast<float>(face_->units_per_EM);
    if (pixelSize_ > 0) {
        const FT_Size_Metrics& sm = face_->size->metrics;
        lm.ascent = sm.ascender / 64.0f;
        lm.descent = -(sm.descender / 64.0f);
        lm.lineGap = (sm.height - (sm.ascender - sm.descender)) / 64.0f;

        // underline / strikeout, scaled to the current size (positive downward).
        const FT_Fixed ys = sm.y_scale;
        lm.underlineOffset = -(FT_MulFix(face_->underline_position, ys) / 64.0f);
        lm.underlineThickness = FT_MulFix(face_->underline_thickness, ys) / 64.0f;
        auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face_, FT_SFNT_OS2));
        if (os2 && os2->version != 0xFFFF) {
            lm.strikeoutOffset = -(FT_MulFix(os2->yStrikeoutPosition, ys) / 64.0f);
            lm.strikeoutThickness = FT_MulFix(os2->yStrikeoutSize, ys) / 64.0f;
        } else {
            lm.strikeoutOffset = -(lm.ascent * 0.3f);
            lm.strikeoutThickness = lm.underlineThickness;
        }
        if (lm.underlineThickness < 1.f) lm.underlineThickness = 1.f;
        if (lm.strikeoutThickness < 1.f) lm.strikeoutThickness = 1.f;
    }
    return lm;
}

namespace {
struct DecompCtx { OutlineSink* sink; };
int mv(const FT_Vector* to, void* u) {
    static_cast<DecompCtx*>(u)->sink->moveTo(static_cast<float>(to->x), static_cast<float>(to->y));
    return 0;
}
int ln(const FT_Vector* to, void* u) {
    static_cast<DecompCtx*>(u)->sink->lineTo(static_cast<float>(to->x), static_cast<float>(to->y));
    return 0;
}
int cn(const FT_Vector* c, const FT_Vector* to, void* u) {
    static_cast<DecompCtx*>(u)->sink->quadTo(static_cast<float>(c->x), static_cast<float>(c->y),
                                             static_cast<float>(to->x), static_cast<float>(to->y));
    return 0;
}
int cb(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* u) {
    static_cast<DecompCtx*>(u)->sink->cubicTo(static_cast<float>(c1->x), static_cast<float>(c1->y),
                                              static_cast<float>(c2->x), static_cast<float>(c2->y),
                                              static_cast<float>(to->x), static_cast<float>(to->y));
    return 0;
}
} // namespace

bool Face::glyphOutline(GlyphId gid, OutlineSink& sink, bool bold, bool italic) const {
    LibraryLock lock(*lib_);
    if (FT_Load_Glyph(face_, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0)
        return false;
    if (face_->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return false;
    if (bold) FT_GlyphSlot_Embolden(face_->glyph);
    if (italic) FT_GlyphSlot_Oblique(face_->glyph);
    FT_Outline_Funcs funcs;
    funcs.move_to = mv;
    funcs.line_to = ln;
    funcs.conic_to = cn;
    funcs.cubic_to = cb;
    funcs.shift = 0;
    funcs.delta = 0;
    DecompCtx ctx{&sink};
    return FT_Outline_Decompose(&face_->glyph->outline, &funcs, &ctx) == 0;
}

bool Face::glyphBitmap(GlyphId gid, bool color, GlyphBitmap& out, bool bold, bool italic) {
    LibraryLock lock(*lib_);
    FT_Int32 flags = color ? FT_LOAD_COLOR : FT_LOAD_DEFAULT;
    if (FT_Load_Glyph(face_, gid, flags) != 0) return false;
    FT_GlyphSlot slot = face_->glyph;
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        if (bold) FT_GlyphSlot_Embolden(slot);
        if (italic) FT_GlyphSlot_Oblique(slot);
    }
    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) return false;
    }
    const FT_Bitmap& bm = slot->bitmap;
    switch (bm.pixel_mode) {
        case FT_PIXEL_MODE_BGRA: out.format = BitmapFormat::BGRA; break;
        case FT_PIXEL_MODE_MONO: out.format = BitmapFormat::Mono; break;
        default: out.format = BitmapFormat::Gray; break;
    }
    out.left = slot->bitmap_left;
    out.top = slot->bitmap_top;
    out.width = static_cast<int>(bm.width);
    out.rows = static_cast<int>(bm.rows);
    out.pitch = bm.pitch;
    out.buffer = bm.buffer;
    return true;
}

} // namespace glyphware
