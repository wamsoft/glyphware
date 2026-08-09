# glyphware roadmap

## Done — Milestone A (standalone library)

- `Library` — one shared, refcounted, mutex-guarded `FT_Library` (no destructor
  teardown hazard).
- `FontBlob` / `FontLoader` — host-injected byte source (no storage dependency).
- `Face` — memory face; metadata (name / OS-2 / head / fvar / cmap coverage);
  glyph index, metrics, line metrics; outline decomposition; bitmap (gray / mono
  / BGRA color); HarfBuzz font.
- `Shaper` — HarfBuzz single-run shaping (glyph ids + positions + clusters).
- `Registry` — declared / runtime registration, lazy metadata resolve, name
  lookup, rich `FontQuery` (name + style + language/script + covered characters,
  answered from compact cmap coverage without opening the font).

Shaping currently uses HarfBuzz's built-in Unicode data (`hb-ucd`); **no ICU is
required** for single-run shaping.

## In progress — Milestone A+ : BiDi + text layout

The core `drawText` path should support **BiDi display** too, so glyphware grows
a small layout layer above single-run shaping. The concerns are deliberately
split behind **swappable seams** so each can be replaced independently:

- **BiDi** (logical → visual reordering) — implemented with **SheenBidi** (MIT,
  self-contained, tiny; no ICU). This is the current addition.
- **Line/word breaking** (break opportunities; Thai/Khmer/CJK have no spaces) —
  a **separate** provider. A naive breaker (spaces + CJK boundaries) now; a
  full breaker (ICU `brkitr`, or a dictionary/rule breaker built inside
  glyphware) can be dropped in later **without touching the BiDi backend**.
- **Itemization** (splitting mixed script/style/face runs) — the fallback/run
  splitter that feeds the shaper.

So: SheenBidi covers BiDi now; a heavier Unicode-data dependency (ICU) is added
only when real line-breaking/itemization is wanted — and only the line-breaker
seam changes, not BiDi.

### Reference: minimal ICU build (for the line-breaking/itemization stage)

Real script itemization and dictionary/rule line breaking need Unicode data
beyond what HarfBuzz bundles — i.e. **ICU**. When that stage arrives, reuse the
existing minimal-ICU setup in the kirikiri tree:

### Reference: minimal ICU build ("character-data only")

A working minimal-ICU setup already exists in the kirikiri tree and should be
reused when glyphware grows the layout layer:

- `krkr_richtext/richtext/ext/minikin/ext/icu/CMakeLists.txt` — FetchContents ICU
  and builds `common` + `i18n` sources into a single static `icucommon`
  (aliased `ICU::common` / `ICU::uc`, which also satisfies HarfBuzz's
  `find_package(ICU)`). Defines `U_STATIC_IMPLEMENTATION` /
  `U_COMMON_IMPLEMENTATION` / `U_I18N_IMPLEMENTATION`.
- `krkr_richtext/richtext/ext/minikin/icudata/` — the important part: a
  **filtered** ICU data package (`filters.json` keeps only what's needed — break
  iterators for the used locales, `misc`, locale-tag data), built to a platform-
  independent little-endian `.dat`, **gzip-bundled** (`icudt77l.dat.gz`) and, at
  build time, expanded by `gen_icudata_c.py` (Python only — no ICU native tools)
  into a C source defining the `icudt77_dat` entry-point symbol, compiled into an
  `icudata` static lib. Normal builds need **only Python 3**; ICU native tools
  are needed solely to regenerate the `.dat` (`make update-bundle`).

So the ICU dependency is added as: `icucommon` (common+i18n from source) +
`icudata` (filtered, bundled, Python-expanded). HarfBuzz can optionally be
pointed at ICU unicode funcs via the `ICU::uc` alias, but glyphware's current
shaping does not require it.
