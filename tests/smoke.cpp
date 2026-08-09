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

    std::printf("PASS\n");
    return 0;
}
