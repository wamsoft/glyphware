#include "glyphware/Registry.h"

#include <algorithm>
#include <cctype>

namespace glyphware {
namespace {

std::string toLower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Decode UTF-8 to codepoints (lenient; skips malformed bytes).
std::vector<char32_t> utf8ToCodepoints(std::string_view s) {
    std::vector<char32_t> out;
    std::size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
        else { ++i; continue; }
        if (i + extra >= n) break;
        for (int k = 0; k < extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + 1 + k]);
            if ((cc >> 6) != 0x2) { cp = 0; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += extra + 1;
        if (cp) out.push_back(cp);
    }
    return out;
}

// Ranges are sorted ascending and non-overlapping (built in cmap order).
bool rangesCover(const std::vector<CodepointRange>& r, char32_t cp) {
    std::size_t lo = 0, hi = r.size();
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        if (cp < r[mid].lo) hi = mid;
        else if (cp > r[mid].hi) lo = mid + 1;
        else return true;
    }
    return false;
}

bool containsCI(const std::vector<std::string>& v, std::string_view needle) {
    std::string ln = toLower(needle);
    for (auto& s : v) if (toLower(s) == ln) return true;
    return false;
}

} // namespace

Registry::Registry(std::shared_ptr<FontLoader> loader) : loader_(std::move(loader)) {}

int Registry::add(FontEntry entry, bool resolveNow) {
    if (entry.descriptor.key.empty()) entry.descriptor.key = entry.key;
    entry.descriptor.faceIndex = entry.faceIndex;
    int id = static_cast<int>(entries_.size());
    entries_.push_back(Slot{std::move(entry), {}});
    if (resolveNow) resolve(id);
    return id;
}

int Registry::registerKey(std::string key, int faceIndex, std::vector<std::string> aliases) {
    FontEntry e;
    e.key = key;
    e.faceIndex = faceIndex;
    e.aliases = std::move(aliases);
    e.descriptor.key = std::move(key);
    e.descriptor.faceIndex = faceIndex;
    return add(std::move(e), false);
}

bool Registry::resolve(int entryId) {
    if (entryId < 0 || entryId >= static_cast<int>(entries_.size())) return false;
    Slot& s = entries_[entryId];
    if (s.e.descriptor.metadataResolved) return true;
    auto f = face(entryId);
    if (!f) return false;
    // Merge: resolved SFNT metadata is authoritative, but keep declared scripts
    // when the font itself carries none (we do not derive scripts from cmap).
    std::vector<std::string> declaredScripts = s.e.descriptor.scripts;
    FontDescriptor merged = f->descriptor();
    if (merged.scripts.empty()) merged.scripts = std::move(declaredScripts);
    s.e.descriptor = std::move(merged);
    return true;
}

std::shared_ptr<Face> Registry::face(int entryId) {
    if (entryId < 0 || entryId >= static_cast<int>(entries_.size())) return nullptr;
    Slot& s = entries_[entryId];
    if (auto sp = s.cache.lock()) return sp;
    if (!loader_) return nullptr;
    auto blob = loader_->load(s.e.key);
    if (!blob) return nullptr;
    auto f = Face::open(blob, s.e.key, s.e.faceIndex);
    s.cache = f;
    return f;
}

std::vector<int> Registry::findByName(std::string_view name) const {
    std::string ln = toLower(name);
    std::vector<int> exact, partial;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const FontDescriptor& d = entries_[i].e.descriptor;
        std::vector<std::string> names = {d.family, d.typographicFamily, d.fullName, d.postScriptName};
        for (auto& a : entries_[i].e.aliases) names.push_back(a);
        bool isExact = false, isPartial = false;
        for (auto& nm : names) {
            if (nm.empty()) continue;
            std::string lnm = toLower(nm);
            if (lnm == ln) { isExact = true; break; }
            if (lnm.find(ln) != std::string::npos) isPartial = true;
        }
        if (isExact) exact.push_back(i);
        else if (isPartial) partial.push_back(i);
    }
    exact.insert(exact.end(), partial.begin(), partial.end());
    return exact;
}

void Registry::ensureFor(int id, const FontQuery& q) {
    Slot& s = entries_[id];
    if (s.e.descriptor.metadataResolved) return;
    // Trust declared metadata: a declared style block (weight/flags/...) and
    // declared scripts/ranges answer their constraints without opening the
    // font. Only resolve what the declaration leaves unknown.
    const bool styleUnknown = !s.e.descriptor.styleDeclared;
    const bool needsStyle =
        (q.weight || q.width || q.slant || q.monospace || q.color) && styleUnknown;
    const bool needsScript = q.script && s.e.descriptor.scripts.empty();
    const bool needsCoverage = !q.containsText.empty() || !q.containsCodepoints.empty();
    const bool coverageUnknown = needsCoverage && s.e.descriptor.ranges.empty();
    if (needsStyle || needsScript || coverageUnknown) resolve(id);
}

std::vector<int> Registry::query(const FontQuery& q) {
    // Gather required codepoints once.
    std::vector<char32_t> required = utf8ToCodepoints(q.containsText);
    required.insert(required.end(), q.containsCodepoints.begin(), q.containsCodepoints.end());

    std::string qname = q.name ? toLower(*q.name) : std::string();

    struct Cand { int id; long score; };
    std::vector<Cand> cands;

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        ensureFor(i, q);
        const FontDescriptor& d = entries_[i].e.descriptor;
        long score = 0;

        // name (disqualify if requested and no match)
        if (q.name) {
            std::vector<std::string> names = {d.family, d.typographicFamily, d.fullName, d.postScriptName};
            for (auto& a : entries_[i].e.aliases) names.push_back(a);
            bool exact = false, partial = false;
            for (auto& nm : names) {
                if (nm.empty()) continue;
                std::string lnm = toLower(nm);
                if (lnm == qname) { exact = true; break; }
                if (lnm.find(qname) != std::string::npos) partial = true;
            }
            if (exact) score += 1000;
            else if (partial) score += 400;
            else continue;
        }

        // style
        if (q.weight) score -= std::abs(static_cast<int>(*q.weight) - static_cast<int>(d.weight));
        if (q.width) score -= std::abs(static_cast<int>(*q.width) - static_cast<int>(d.width)) * 10;
        if (q.slant) score += (*q.slant == d.slant) ? 300 : -300;

        // flags (hard)
        if (q.monospace && *q.monospace != d.monospace) continue;
        if (q.color && *q.color != d.color) continue;

        // declared script (filter only when the entry declares scripts)
        if (q.script && !d.scripts.empty()) {
            if (containsCI(d.scripts, *q.script)) score += 200;
            else continue;
        }

        // coverage (hard: must cover every required codepoint)
        if (!required.empty()) {
            bool ok = !d.ranges.empty();
            for (char32_t cp : required) {
                if (!rangesCover(d.ranges, cp)) { ok = false; break; }
            }
            if (!ok) continue;
            score += 100;
        }

        cands.push_back({i, score});
    }

    std::stable_sort(cands.begin(), cands.end(),
                     [](const Cand& a, const Cand& b) { return a.score > b.score; });
    std::vector<int> out;
    out.reserve(cands.size());
    for (auto& c : cands) out.push_back(c.id);
    return out;
}

std::shared_ptr<Face> Registry::queryFace(const FontQuery& q) {
    for (int id : query(q)) {
        if (auto f = face(id)) return f;
    }
    return nullptr;
}

} // namespace glyphware
