// glyphware — font registry + rich query.
//
// The Registry is the name/metadata index over registered fonts. Fonts are
// registered three ways, all host-driven (glyphware never enumerates storage
// itself): declared up front (a fonts.json-style manifest the host parses),
// scanned (the host enumerates an archive/dir and registers each), or at
// runtime from script. Each entry is a font key + faceIndex plus optional
// declared metadata; real SFNT metadata is parsed lazily on first need.
//
// Query answers "which font best matches name + style + language/script + the
// characters I need to render", using declared/compact cmap coverage so the
// common case needs no font open.
#ifndef GLYPHWARE_REGISTRY_H
#define GLYPHWARE_REGISTRY_H

#include "Blob.h"
#include "Descriptor.h"
#include "Face.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace glyphware {

// A registered font. `aliases` are extra lookup names (e.g. a filename stem in
// addition to the embedded family). `descriptor` starts as whatever the host
// declared (possibly just the key) and is completed on resolve().
struct FontEntry {
    std::string key;                    // loader key (opaque host identifier)
    int faceIndex = 0;
    std::vector<std::string> aliases;
    FontDescriptor descriptor;          // declared-or-resolved
};

class Registry {
public:
    explicit Registry(std::shared_ptr<FontLoader> loader);

    // Register a font. Declared fields in `entry.descriptor` (family, scripts,
    // ranges, flags, style) are kept; missing ones are filled by resolve().
    // Returns the entry id. If resolveNow, the font is opened immediately.
    int add(FontEntry entry, bool resolveNow = false);

    // Shorthand: register by key alone; metadata resolved on demand.
    int registerKey(std::string key, int faceIndex = 0,
                    std::vector<std::string> aliases = {});

    // Parse full SFNT metadata for an entry (opens the font once). Idempotent.
    bool resolve(int entryId);

    // Cached Face for an entry (shared; opened via the loader). null on failure.
    std::shared_ptr<Face> face(int entryId);

    // Entry ids whose name (family / typographic / full / PostScript / alias)
    // matches `name` case-insensitively (exact first, then substring).
    std::vector<int> findByName(std::string_view name) const;

    // Ranked entry ids for a query, best first. Disqualifying constraints
    // (uncovered required characters, wrong monospace/color, unmatched declared
    // script, unmatched requested name) drop candidates.
    std::vector<int> query(const FontQuery& q);

    // Best Face for a query (opens it), or nullptr.
    std::shared_ptr<Face> queryFace(const FontQuery& q);

    const FontEntry& entry(int id) const { return entries_[id].e; }
    std::size_t size() const { return entries_.size(); }

private:
    struct Slot {
        FontEntry e;
        std::weak_ptr<Face> cache;
    };
    std::shared_ptr<FontLoader> loader_;
    std::vector<Slot> entries_;

    // Ensure enough metadata is present to evaluate `q` for this entry.
    void ensureFor(int id, const FontQuery& q);
};

} // namespace glyphware

#endif // GLYPHWARE_REGISTRY_H
