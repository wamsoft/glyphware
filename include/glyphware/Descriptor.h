// glyphware — font metadata (FontDescriptor) and rich query.
#ifndef GLYPHWARE_DESCRIPTOR_H
#define GLYPHWARE_DESCRIPTOR_H

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace glyphware {

// OpenType weight class (OS/2 usWeightClass), 1..1000. 400=Regular, 700=Bold.
enum class Weight : int {
    Thin = 100, ExtraLight = 200, Light = 300, Regular = 400, Medium = 500,
    SemiBold = 600, Bold = 700, ExtraBold = 800, Black = 900,
};

// OpenType width class (OS/2 usWidthClass), 1..9. 5=Normal.
enum class Width : int {
    UltraCondensed = 1, ExtraCondensed = 2, Condensed = 3, SemiCondensed = 4,
    Normal = 5, SemiExpanded = 6, Expanded = 7, ExtraExpanded = 8, UltraExpanded = 9,
};

enum class Slant { Normal, Italic, Oblique };

// A closed codepoint interval [lo, hi] (inclusive), as in fonts.json "ranges".
struct CodepointRange {
    char32_t lo;
    char32_t hi;
};

// A named variable-font axis (fvar).
struct VarAxis {
    std::uint32_t tag;   // e.g. 'wght','wdth','ital','slnt','opsz' (big-endian packed)
    float minValue;
    float defaultValue;
    float maxValue;
    std::string name;
};

// A named instance (fvar named style): a designer-provided preset of axis
// values with a subfamily name (e.g. "SemiBold", "Condensed Bold").
struct NamedInstance {
    std::string name;
    // (axis tag, design value) in fvar axis order; one entry per axis.
    std::vector<std::pair<std::uint32_t, float>> coords;
};

// Immutable metadata for one face (one font file + faceIndex). Populated lazily:
// declared entries carry only what the host declared until first real parse.
struct FontDescriptor {
    std::string key;        // host font key (loader identifier)
    int faceIndex = 0;

    // name table
    std::string family;             // name id 1  (or 16 typographic if present)
    std::string subfamily;          // name id 2  (or 17)
    std::string fullName;           // name id 4
    std::string postScriptName;     // name id 6
    std::string typographicFamily;  // name id 16 (if any)

    // style (OS/2 + head)
    Weight weight = Weight::Regular;
    Width  width  = Width::Normal;
    Slant  slant  = Slant::Normal;
    bool   bold   = false;

    // classification / capability
    std::vector<std::string> scripts;   // ISO-15924 tags (declared or derived)
    std::vector<std::string> languages; // optional BCP47 hints
    bool color = false;                 // CBDT/COLR/sbix present
    bool monospace = false;             // fixed pitch
    bool scalable = true;

    // coverage
    std::vector<CodepointRange> ranges; // cmap coverage (compact)

    // variable font
    std::vector<VarAxis> axes;
    std::vector<NamedInstance> namedInstances;   // fvar named styles (empty for non-VF)

    bool metadataResolved = false;      // false = declared-only, not yet FT-parsed
    bool styleDeclared = false;         // weight/width/slant/color/monospace came from a
                                        // declaration and can be trusted without resolving
};

// Rich query. Unset (nullopt / empty) fields do not constrain. `containsText`
// (UTF-8) / `containsCodepoints` require the candidate to cover every listed
// codepoint. Scoring biases exact name > style proximity > script/coverage.
struct FontQuery {
    std::optional<std::string> name;        // family / alias / fullName / PS name
    std::optional<Weight> weight;
    std::optional<Width>  width;
    std::optional<Slant>  slant;
    std::optional<std::string> script;      // ISO-15924
    std::optional<std::string> language;    // BCP47
    std::string containsText;               // UTF-8; must be fully covered
    std::vector<char32_t> containsCodepoints;
    std::optional<bool> monospace;
    std::optional<bool> color;
    bool allowSystem = false;               // include host/system fonts (future)
};

} // namespace glyphware

#endif // GLYPHWARE_DESCRIPTOR_H
