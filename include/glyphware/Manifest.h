// glyphware — font manifest (fonts.json) parsing.
//
// Parses a declarative font manifest (compatible with 吉里吉里Z's data/fonts.json
// plus optional style fields) into Registry entries WITHOUT opening any font:
// the declared family / aliases / scripts / cmap ranges / flags let queries
// (including "contains these characters") run against the manifest alone; real
// SFNT metadata is parsed lazily by the Registry only when actually needed.
//
// Schema:
//   {
//     "version": 1,
//     "fonts": [
//       { "file": "fonts/foo.ttf",        // -> loader key
//         "family": "Foo",                 // declared family
//         "aliases": ["foo-stem"],
//         "scripts": ["Jpan"],             // ISO-15924
//         "ranges": [[0x4E00,0x9FFF]],     // cmap coverage
//         "flags": ["color","monospace"],
//         "weight": 700, "width": 5, "italic": true,
//         "faceIndex": 0, "languages": ["ja"] }
//     ]
//   }
// `file` (or `key`) is required; everything else is optional.
#ifndef GLYPHWARE_MANIFEST_H
#define GLYPHWARE_MANIFEST_H

#include "Registry.h"

#include <string>
#include <string_view>
#include <vector>

namespace glyphware {

// Parse a fonts.json document into FontEntry list. On JSON error, returns an
// empty vector and sets `error` (if non-null). No fonts are opened.
std::vector<FontEntry> parseFontManifest(std::string_view json, std::string* error = nullptr);

// Parse and register every declared font into `reg`. Returns the number added.
int registerFontManifest(Registry& reg, std::string_view json, std::string* error = nullptr);

} // namespace glyphware

#endif // GLYPHWARE_MANIFEST_H
