// glyphware — neutral OTF/TTF font engine (FreeType + HarfBuzz).
//
// A host-agnostic library that owns one shared FreeType library, opens font
// faces over host-supplied byte blobs, extracts rich metadata, supplies glyph
// outlines AND bitmaps, and shapes text with HarfBuzz. It has no dependency on
// storage or rendering: the byte source is injected via FontLoader, and the
// consumer decides what to do with outlines/bitmaps/shaped runs.
//
// Umbrella include.
#ifndef GLYPHWARE_H
#define GLYPHWARE_H

#define GLYPHWARE_VERSION_MAJOR 0
#define GLYPHWARE_VERSION_MINOR 1
#define GLYPHWARE_VERSION_PATCH 0

#include "Blob.h"
#include "Descriptor.h"
#include "Library.h"
#include "Face.h"
#include "Shaper.h"
#include "Registry.h"
#include "Bidi.h"
#include "Layout.h"

#endif // GLYPHWARE_H
