# glyphware

A neutral OTF/TTF **font engine** on top of **FreeType + HarfBuzz**. It provides,
from one shared FreeType library:

- **rich metadata** per face — name table (family / subfamily / full / PostScript
  / typographic), OS/2 weight-width-slant, color/monospace flags, variable-font
  axes, and compact cmap coverage ranges;
- **glyph geometry** in both shapes a consumer might need — vector **outlines**
  (for vector text / ThorVG) and rendered **bitmaps** (AA / mono / BGRA color);
- **HarfBuzz text shaping** (glyph ids + positioned advances/offsets + clusters);
- a **rich query** surface (name + style + language/script + covered codepoints).

glyphware is **host-agnostic**: it never touches the filesystem or any archive.
The host supplies font bytes through a `FontLoader`, so a host with its own
shared on-memory cache can hand back blobs that alias its cached buffers (no
copy). It knows nothing about 吉里吉里 — it is intended to be consumed equally by
the 吉里吉里Z engine core, its Elements/ThorVG UI, and the layerExVector plugin.

## Design

See the umbrella design plan (kirikiri font-unification) — glyphware is the
"unified font engine library" that replaces the three-plus independent
FreeType/HarfBuzz stacks scattered across the core and plugins.

## Third-party libraries and licenses

glyphware links the following third-party libraries. When distributing a binary
that includes glyphware, retain these libraries' license notices.

| Library | Used for | License |
|---|---|---|
| [FreeType](https://freetype.org/) | face loading, glyph outlines & bitmaps, metadata (SFNT/OS-2/fvar/cmap) | The FreeType License (FTL, BSD-style with a credit clause) or GPLv2 — dual-licensed |
| [HarfBuzz](https://harfbuzz.github.io/) | text shaping | MIT ("Old MIT" license) |
| [SheenBidi](https://github.com/Tehreer/SheenBidi) | Unicode Bidirectional Algorithm (BiDi) | Apache-2.0 |
| [picojson](https://github.com/kazuho/picojson) | fonts.json manifest parsing (header-only) | BSD-2-Clause |

Pulled in transitively by FreeType (via vcpkg): **zlib** (zlib license),
**libpng** (PNG Reference Library license), **bzip2** (bzip2 license).

FreeType and HarfBuzz are provided by the vcpkg manifest (`vcpkg.json`);
SheenBidi is fetched by CMake `FetchContent` from the [wtnbgo fork](https://github.com/wtnbgo/SheenBidi)
(`custom` branch = `v3.0.0` + an `SB_INSTALL` option so embedding projects can
suppress its install rules; see `GLYPHWARE_INSTALL` below). The BiDi
backend is isolated behind `src/Bidi_sheenbidi.cpp`, so SheenBidi can be swapped
(e.g. for ICU) without affecting the rest.

> glyphware's own license is to be set by the project owner (Wamsoft); this
> section records only the third-party dependencies' terms.

## Build

Requires a C++17 compiler, CMake ≥ 3.20, and vcpkg (`VCPKG_ROOT` set). FreeType
and HarfBuzz are pulled by the vcpkg manifest (`vcpkg.json`); SheenBidi is
fetched via `FetchContent`.

```sh
cmake --preset x64-windows
cmake --build build/x64-windows --config Release
ctest --test-dir build/x64-windows -C Release --output-on-failure
```

CMake options: `GLYPHWARE_BUILD_TESTS` (default ON) builds the tests;
`GLYPHWARE_INSTALL` (default ON) generates install rules for glyphware and the
bundled SheenBidi — parent projects embedding glyphware via `add_subdirectory`
can set it to OFF to keep both out of their install tree.

The `glyphware_smoke` test opens a real font (a system font by default, or one
passed as its first argument) and exercises metadata / outline / bitmap /
shaping end-to-end.

## Layout

- `include/glyphware/` — public headers (`Blob`, `Descriptor`, `Library`,
  `Face`, `Shaper`, `Bidi`, `Layout`, `Registry`, `Manifest`, umbrella
  `glyphware.h`).
- `src/` — implementation.
- `tests/` — smoke test.

Text layout comes in two levels, both in `glyphware/Layout.h`:

- `layoutLine` — one line: BiDi → face itemization → shaping → positioned
  glyphs in visual order.
- `layoutBlock` — flow into a rectangle: explicit newlines, greedy word /
  per-character (CJK) wrapping with Japanese 行頭/行末禁則, alignment, a height
  bound, and a cluster `count` limit for typewriter reveals. Wrapping is always
  resolved for the full text before `count` is applied, so a reveal never
  reflows.

## Status

Milestone **A** (standalone library): scaffold + shared FreeType library +
host-injected loader seam + face open + metadata extraction + glyph
outline/bitmap + HarfBuzz shaping. Registry + rich-query index are next.
