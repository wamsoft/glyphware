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
#include <cmath>
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
    if (FT_Set_Pixel_Sizes(face_, 0, static_cast<FT_UInt>(pixels)) != 0) {
        // fixed-strike (bitmap-only, e.g. CBDT color emoji) fonts have no
        // scalable size; select the nearest available strike instead. The
        // requested pixel size is still recorded so glyphBitmap() can scale
        // the strike to it.
        if (face_->num_fixed_sizes <= 0) return false;
        int best = 0;
        int bestDelta = 1 << 30;
        for (int i = 0; i < face_->num_fixed_sizes; ++i) {
            int ph = face_->available_sizes[i].height;
            int d = ph > pixels ? ph - pixels : pixels - ph;
            if (d < bestDelta) { bestDelta = d; best = i; }
        }
        if (FT_Select_Size(face_, best) != 0) return false;
    }
    pixelSize_ = pixels;
    if (hb_) hb_ft_font_changed(hb_);
    return true;
}

GlyphId Face::glyphIndex(char32_t cp) const {
    return FT_Get_Char_Index(face_, static_cast<FT_ULong>(cp));
}

bool Face::glyphMetrics(GlyphId gid, GlyphMetrics& out, bool bold, bool italic) const {
    LibraryLock lock(*lib_);
    // For scalable fonts ignore embedded bitmap strikes so the advance is the
    // OUTLINE advance — consistent with glyphBitmap() (which renders outlines for
    // text via FT_LOAD_NO_BITMAP) and with the classic FreeType path. Without
    // this, a font with an embedded bitmap strike at a given ppem (e.g. MS
    // PGothic at 22px) would report the strike's advance while the glyph renders
    // as an outline, so text advances and glyph shapes disagree. Bitmap-only
    // fonts (CBDT color emoji) keep the strike and are scaled below.
    FT_Int32 flags = FT_LOAD_DEFAULT;
    if (face_->face_flags & FT_FACE_FLAG_SCALABLE) flags |= FT_LOAD_NO_BITMAP;
    if (FT_Load_Glyph(face_, gid, flags) != 0) return false;
    FT_GlyphSlot slot = face_->glyph;
    // apply synthetic bold/italic so the advance/metrics match glyphBitmap():
    // FT_GlyphSlot_Embolden widens the advance, so the classic FreeType path
    // (which emboldens before reading the advance) is otherwise wider than us.
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        if (bold) FT_GlyphSlot_Embolden(slot);
        if (italic) FT_GlyphSlot_Oblique(slot);
    }
    const FT_Glyph_Metrics& m = slot->metrics;
    // fixed-strike (bitmap-only, e.g. CBDT color emoji) fonts report metrics at
    // the strike ppem; scale to the requested pixel size to match glyphBitmap().
    float scale = 1.0f;
    if ((face_->face_flags & FT_FACE_FLAG_SCALABLE) == 0 && pixelSize_ > 0 &&
        face_->size && face_->size->metrics.y_ppem > 0) {
        scale = static_cast<float>(pixelSize_) / face_->size->metrics.y_ppem;
    }
    out.advanceX = slot->advance.x / 64.0f * scale;
    out.advanceY = slot->advance.y / 64.0f * scale;
    out.bearingX = m.horiBearingX / 64.0f * scale;
    out.bearingY = m.horiBearingY / 64.0f * scale;
    out.width = m.width / 64.0f * scale;
    out.height = m.height / 64.0f * scale;
    return true;
}

float Face::fixedStrikeScale() const {
    if ((face_->face_flags & FT_FACE_FLAG_SCALABLE) == 0 && pixelSize_ > 0 &&
        face_->size && face_->size->metrics.y_ppem > 0) {
        return static_cast<float>(pixelSize_) / face_->size->metrics.y_ppem;
    }
    return 1.0f;
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

void Face::setTransform(double xx, double xy, double yx, double yy) {
    LibraryLock lock(*lib_);
    FT_Matrix m;
    m.xx = static_cast<FT_Fixed>(std::lround(xx * 65536.0));
    m.xy = static_cast<FT_Fixed>(std::lround(xy * 65536.0));
    m.yx = static_cast<FT_Fixed>(std::lround(yx * 65536.0));
    m.yy = static_cast<FT_Fixed>(std::lround(yy * 65536.0));
    FT_Set_Transform(face_, &m, nullptr);
}

void Face::clearTransform() {
    LibraryLock lock(*lib_);
    FT_Set_Transform(face_, nullptr, nullptr);
}

bool Face::glyphBitmap(GlyphId gid, bool color, GlyphBitmap& out, bool bold, bool italic) {
    LibraryLock lock(*lib_);
    // color=true: allow color bitmaps (CBDT/COLR/sbix) for emoji glyphs.
    // color=false: text -> force the scalable outline with FT_LOAD_NO_BITMAP so
    // embedded MONO bitmap strikes (which CJK fonts carry at small sizes) are
    // ignored, matching the classic FreeType path (anti-aliased outlines) rather
    // than rendering blocky embedded bitmaps.
    FT_Int32 flags = color ? FT_LOAD_COLOR : (FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
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
    const int w = static_cast<int>(bm.width);
    const int h = static_cast<int>(bm.rows);
    out.left = slot->bitmap_left;
    out.top = slot->bitmap_top;

    if (bm.pixel_mode == FT_PIXEL_MODE_BGRA) {
        // color glyph (COLR/CBDT/sbix). Premultiplied BGRA. Bitmap-strike fonts
        // (CBDT/sbix) render at a fixed strike ppem, so scale to the requested
        // pixel size (scalable COLR renders at pixelSize_ already => scale~1).
        const int yppem = face_->size ? static_cast<int>(face_->size->metrics.y_ppem) : 0;
        const double scale = (yppem > 0 && pixelSize_ > 0)
                                 ? static_cast<double>(pixelSize_) / yppem : 1.0;
        if (w > 0 && h > 0 && (scale < 0.99 || scale > 1.01)) {
            int dw = static_cast<int>(w * scale + 0.5); if (dw < 1) dw = 1;
            int dh = static_cast<int>(h * scale + 0.5); if (dh < 1) dh = 1;
            bmpBuf_.assign(static_cast<std::size_t>(dw) * dh * 4, 0);
            const int spitch = bm.pitch;
            for (int dy = 0; dy < dh; ++dy) {
                int sy0 = static_cast<int>(static_cast<std::int64_t>(dy) * h / dh);
                int sy1 = static_cast<int>(static_cast<std::int64_t>(dy + 1) * h / dh);
                if (sy1 <= sy0) sy1 = sy0 + 1; if (sy1 > h) sy1 = h;
                for (int dx = 0; dx < dw; ++dx) {
                    int sx0 = static_cast<int>(static_cast<std::int64_t>(dx) * w / dw);
                    int sx1 = static_cast<int>(static_cast<std::int64_t>(dx + 1) * w / dw);
                    if (sx1 <= sx0) sx1 = sx0 + 1; if (sx1 > w) sx1 = w;
                    std::uint32_t b = 0, g = 0, r = 0, a = 0, cnt = 0;
                    for (int yy = sy0; yy < sy1; ++yy) {
                        const std::uint8_t* sp = bm.buffer + static_cast<std::ptrdiff_t>(spitch) * yy + static_cast<std::ptrdiff_t>(sx0) * 4;
                        for (int xx = sx0; xx < sx1; ++xx) { b += sp[0]; g += sp[1]; r += sp[2]; a += sp[3]; sp += 4; ++cnt; }
                    }
                    std::uint8_t* d = &bmpBuf_[(static_cast<std::size_t>(dy) * dw + dx) * 4];
                    d[0] = static_cast<std::uint8_t>(b / cnt); d[1] = static_cast<std::uint8_t>(g / cnt);
                    d[2] = static_cast<std::uint8_t>(r / cnt); d[3] = static_cast<std::uint8_t>(a / cnt);
                }
            }
            out.left = static_cast<int>(out.left * scale + (out.left >= 0 ? 0.5 : -0.5));
            out.top = static_cast<int>(out.top * scale + (out.top >= 0 ? 0.5 : -0.5));
            out.format = BitmapFormat::BGRA;
            out.width = dw; out.rows = dh; out.pitch = dw * 4; out.buffer = bmpBuf_.data();
            return true;
        }
        out.format = BitmapFormat::BGRA;
        out.width = w; out.rows = h; out.pitch = bm.pitch; out.buffer = bm.buffer;
        return true;
    }

    // grayscale / monochrome -> normalized 8-bit gray coverage (0..255). This
    // covers embedded MONO bitmap strikes (CJK fonts at small sizes) and gray
    // bitmaps whose num_grays != 256, matching the classic FreeType path so a
    // consumer always gets true 8-bit coverage.
    out.format = BitmapFormat::Gray;
    out.width = w; out.rows = h; out.pitch = w;
    if (w <= 0 || h <= 0) { out.pitch = 0; out.buffer = nullptr; return true; }
    bmpBuf_.assign(static_cast<std::size_t>(w) * h, 0);
    if (bm.pixel_mode == FT_PIXEL_MODE_MONO) {
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* src = bm.buffer + static_cast<std::ptrdiff_t>(bm.pitch) * y;
            std::uint8_t* d = bmpBuf_.data() + static_cast<std::size_t>(w) * y;
            for (int x = 0; x < w; ++x) d[x] = (src[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
        }
    } else {
        const int ng = bm.num_grays > 1 ? bm.num_grays : 256;
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* src = bm.buffer + static_cast<std::ptrdiff_t>(bm.pitch) * y;
            std::uint8_t* d = bmpBuf_.data() + static_cast<std::size_t>(w) * y;
            for (int x = 0; x < w; ++x) {
                int v = src[x];
                if (ng != 256) v = v * 255 / (ng - 1);
                d[x] = static_cast<std::uint8_t>(v);
            }
        }
    }
    out.buffer = bmpBuf_.data();
    return true;
}

} // namespace glyphware
