// glyphware smoke test — exercises Library / Face / metadata / outline / bitmap
// / HarfBuzz shaping end-to-end against a real font file. This stands in for a
// host: it reads font bytes from disk into an OwnedFontBlob (a real host would
// hand back a cache-backed blob instead).
//
// Usage: smoke [font.ttf]   (falls back to common system fonts)
// Exit: 0 = pass, 77 = skipped (no usable font found), other = fail.

#include "glyphware/glyphware.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace glyphware;

static std::shared_ptr<FontBlob> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return nullptr;
    return std::make_shared<OwnedFontBlob>(std::move(bytes));
}

// A trivial host loader: the font "key" is a filesystem path.
struct DiskLoader : FontLoader {
    std::shared_ptr<FontBlob> load(std::string_view key) override {
        return readFile(std::string(key));
    }
};

struct CountingSink : OutlineSink {
    int moves = 0, lines = 0, quads = 0, cubics = 0, closes = 0;
    void moveTo(float, float) override { ++moves; }
    void lineTo(float, float) override { ++lines; }
    void quadTo(float, float, float, float) override { ++quads; }
    void cubicTo(float, float, float, float, float, float) override { ++cubics; }
    void close() override { ++closes; }
};

int main(int argc, char** argv) {
    std::vector<std::string> candidates;
    if (argc > 1) candidates.push_back(argv[1]);
    candidates.push_back("C:/Windows/Fonts/segoeui.ttf");
    candidates.push_back("C:/Windows/Fonts/arial.ttf");
    candidates.push_back("C:/Windows/Fonts/msgothic.ttc");
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back("/System/Library/Fonts/Supplemental/Arial.ttf");

    std::shared_ptr<FontBlob> blob;
    std::string used;
    for (auto& c : candidates) {
        blob = readFile(c);
        if (blob) { used = c; break; }
    }
    if (!blob) {
        std::fprintf(stderr, "SKIP: no usable font file found (pass one as argv[1])\n");
        return 77;
    }

    int vmaj = 0, vmin = 0, vpat = 0;
    Library::instance()->version(vmaj, vmin, vpat);
    std::printf("glyphware %d.%d.%d  FreeType %d.%d.%d  font=%s\n",
                GLYPHWARE_VERSION_MAJOR, GLYPHWARE_VERSION_MINOR, GLYPHWARE_VERSION_PATCH,
                vmaj, vmin, vpat, used.c_str());

    auto face = Face::open(blob, used, 0);
    if (!face) { std::fprintf(stderr, "FAIL: Face::open\n"); return 1; }

    const FontDescriptor& d = face->descriptor();
    std::printf("descriptor: family='%s' subfamily='%s' full='%s' ps='%s'\n",
                d.family.c_str(), d.subfamily.c_str(), d.fullName.c_str(), d.postScriptName.c_str());
    std::printf("  weight=%d width=%d slant=%d bold=%d color=%d mono=%d scalable=%d\n",
                (int)d.weight, (int)d.width, (int)d.slant, d.bold, d.color, d.monospace, d.scalable);
    std::printf("  cmap ranges=%zu  var axes=%zu\n", d.ranges.size(), d.axes.size());
    if (d.family.empty()) { std::fprintf(stderr, "FAIL: empty family\n"); return 2; }
    if (d.ranges.empty()) { std::fprintf(stderr, "FAIL: empty cmap coverage\n"); return 3; }

    if (!face->setPixelSize(32)) { std::fprintf(stderr, "FAIL: setPixelSize\n"); return 4; }
    LineMetrics lm = face->lineMetrics();
    std::printf("  line: ascent=%.1f descent=%.1f gap=%.1f upem=%.0f\n",
                lm.ascent, lm.descent, lm.lineGap, lm.unitsPerEm);

    GlyphId gA = face->glyphIndex(U'A');
    std::printf("  glyphIndex('A')=%u covers('A')=%d covers(U+4E00)=%d\n",
                gA, face->covers(U'A'), face->covers(U'一'));
    if (gA == 0) { std::fprintf(stderr, "FAIL: no glyph for 'A'\n"); return 5; }

    GlyphMetrics gm;
    if (face->glyphMetrics(gA, gm))
        std::printf("  'A' metrics: adv=%.1f bearing=(%.1f,%.1f) size=(%.1f,%.1f)\n",
                    gm.advanceX, gm.bearingX, gm.bearingY, gm.width, gm.height);

    CountingSink sink;
    if (!face->glyphOutline(gA, sink)) { std::fprintf(stderr, "FAIL: glyphOutline\n"); return 6; }
    std::printf("  'A' outline: moves=%d lines=%d quads=%d cubics=%d closes=%d\n",
                sink.moves, sink.lines, sink.quads, sink.cubics, sink.closes);
    if (sink.moves == 0) { std::fprintf(stderr, "FAIL: empty outline\n"); return 7; }

    GlyphBitmap bmp;
    if (face->glyphBitmap(gA, false, bmp))
        std::printf("  'A' bitmap: fmt=%d %dx%d pitch=%d left=%d top=%d\n",
                    (int)bmp.format, bmp.width, bmp.rows, bmp.pitch, bmp.left, bmp.top);

    std::vector<ShapedGlyph> shaped;
    ShapeOptions opts;
    shapeRun(*face, "Hello, world", opts, shaped);
    std::printf("  shaped 'Hello, world' -> %zu glyphs; advances:", shaped.size());
    for (auto& g : shaped) std::printf(" %.1f", g.xAdvance);
    std::printf("\n");
    if (shaped.empty()) { std::fprintf(stderr, "FAIL: shaping produced no glyphs\n"); return 8; }

    // Registry + rich query
    {
        auto loader = std::make_shared<DiskLoader>();
        Registry reg(loader);
        reg.registerKey(used, 0, {"smoke-alias"});

        FontQuery q;
        q.name = d.family;
        q.weight = Weight::Regular;
        auto ranked = reg.query(q);
        std::printf("  registry: entries=%zu  query(name='%s',weight=Regular) -> %zu\n",
                    reg.size(), d.family.c_str(), ranked.size());
        if (ranked.empty()) { std::fprintf(stderr, "FAIL: registry name query empty\n"); return 9; }

        FontQuery qcov;      qcov.containsText = "Hi";               // must be covered
        FontQuery qbad;      qbad.containsCodepoints = {0x10FFFD};   // almost certainly absent
        auto covered = reg.query(qcov);
        auto uncovered = reg.query(qbad);
        std::printf("  registry: containsText('Hi') -> %zu ; containsCodepoints(U+10FFFD) -> %zu\n",
                    covered.size(), uncovered.size());
        if (covered.empty()) { std::fprintf(stderr, "FAIL: coverage dropped a covering font\n"); return 10; }
        if (!uncovered.empty()) { std::fprintf(stderr, "FAIL: coverage kept a non-covering font\n"); return 11; }

        auto qf = reg.queryFace(q);
        if (!qf) { std::fprintf(stderr, "FAIL: queryFace null\n"); return 12; }
        std::printf("  registry: queryFace -> family='%s'\n", qf->descriptor().family.c_str());

        if (reg.findByName("smoke-alias").empty()) {
            std::fprintf(stderr, "FAIL: alias lookup\n"); return 13;
        }
    }

    // BiDi (logical -> visual runs). "abc" + Hebrew alef/bet/gimel + "def".
    {
        std::string bidi = std::string("abc") + "\xD7\x90\xD7\x91\xD7\x92" + "def";
        BidiResult br = bidiAnalyze(bidi, BaseDirection::LTR);
        std::printf("  bidi: paraLevel=%d runs=%zu :", br.paragraphLevel, br.runs.size());
        for (auto& r : br.runs)
            std::printf(" [%zu+%zu L%d %s]", r.offset, r.length, r.level, r.rtl ? "RTL" : "LTR");
        std::printf("\n");
        if (br.runs.size() != 3) {
            std::fprintf(stderr, "FAIL: bidi run count %zu != 3\n", br.runs.size()); return 14;
        }
        if (br.runs[0].rtl || !br.runs[1].rtl || br.runs[2].rtl) {
            std::fprintf(stderr, "FAIL: bidi run directions\n"); return 15;
        }
        if (br.paragraphLevel != 0) { std::fprintf(stderr, "FAIL: bidi base level\n"); return 16; }

        // Auto base direction with a leading RTL char -> RTL paragraph.
        std::string rtlFirst = std::string("\xD7\x90\xD7\x91") + "AB";
        BidiResult br2 = bidiAnalyze(rtlFirst, BaseDirection::Auto);
        std::printf("  bidi(auto, RTL-first): paraLevel=%d runs=%zu\n", br2.paragraphLevel, br2.runs.size());
        if (br2.paragraphLevel != 1) { std::fprintf(stderr, "FAIL: auto base should be RTL\n"); return 17; }
    }

    // Layout: BiDi + face itemization + shaping -> visual-order positioned glyphs
    {
        std::vector<std::shared_ptr<Face>> chain = {face};
        std::string mixed = std::string("abc") + "\xD7\x90\xD7\x91\xD7\x92" + "def";
        LineLayout ll = layoutLine(mixed, BaseDirection::Auto, chain, 32);
        int rtl = 0;
        for (auto& g : ll.glyphs) if (g.rtl) ++rtl;
        std::printf("  layout: glyphs=%zu width=%.1f ascent=%.1f descent=%.1f rtlGlyphs=%d\n",
                    ll.glyphs.size(), ll.width, ll.ascent, ll.descent, rtl);
        if (ll.glyphs.empty()) { std::fprintf(stderr, "FAIL: layout no glyphs\n"); return 18; }
        if (ll.width <= 0.f) { std::fprintf(stderr, "FAIL: layout width\n"); return 19; }
        if (rtl == 0) { std::fprintf(stderr, "FAIL: layout has no RTL glyphs\n"); return 20; }
        // x positions must be non-decreasing (visual order, left to right).
        for (std::size_t i = 1; i < ll.glyphs.size(); ++i)
            if (ll.glyphs[i].x < ll.glyphs[i - 1].x - 0.01f) {
                std::fprintf(stderr, "FAIL: layout x not monotonic\n"); return 21;
            }
    }

    // Manifest (fonts.json) — declared metadata, coverage without opening
    {
        auto loader = std::make_shared<DiskLoader>();
        Registry reg(loader);
        std::string json = std::string("{\"version\":1,\"fonts\":[{")
            + "\"file\":\"" + used + "\","
            + "\"family\":\"Declared Family\",\"aliases\":[\"decl-alias\"],"
            + "\"scripts\":[\"Latn\"],\"weight\":700,\"width\":5,\"italic\":true,"
            + "\"flags\":[\"monospace\"],\"ranges\":[[65,90],[97,122]]"  // A-Z a-z
            + "}]}";
        std::string err;
        int added = registerFontManifest(reg, json, &err);
        std::printf("  manifest: added=%d err='%s'\n", added, err.c_str());
        if (added != 1) { std::fprintf(stderr, "FAIL: manifest add (%s)\n", err.c_str()); return 22; }

        // Declared ranges answer coverage without opening the font.
        FontQuery qA; qA.containsCodepoints = {U'A'};
        FontQuery q0; q0.containsCodepoints = {U'0'};   // 0x30, outside declared ranges
        std::size_t nA = reg.query(qA).size();
        std::size_t n0 = reg.query(q0).size();
        std::printf("  manifest: containsA=%zu contains0=%zu\n", nA, n0);
        if (nA != 1) { std::fprintf(stderr, "FAIL: declared coverage A\n"); return 23; }
        if (n0 != 0) { std::fprintf(stderr, "FAIL: declared coverage 0 should drop\n"); return 24; }
        if (reg.findByName("decl-alias").empty() || reg.findByName("Declared Family").empty()) {
            std::fprintf(stderr, "FAIL: manifest name/alias\n"); return 25;
        }
    }

    std::printf("PASS\n");
    return 0;
}
