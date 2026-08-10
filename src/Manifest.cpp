#include "glyphware/Manifest.h"

#include <algorithm>

#include <picojson.h>

namespace glyphware {
namespace {

std::string getString(const picojson::object& o, const char* k) {
    auto it = o.find(k);
    if (it != o.end() && it->second.is<std::string>()) return it->second.get<std::string>();
    return {};
}

bool getNumber(const picojson::object& o, const char* k, double& out) {
    auto it = o.find(k);
    if (it != o.end() && it->second.is<double>()) { out = it->second.get<double>(); return true; }
    return false;
}

std::vector<std::string> getStringArray(const picojson::object& o, const char* k) {
    std::vector<std::string> out;
    auto it = o.find(k);
    if (it != o.end() && it->second.is<picojson::array>()) {
        for (const auto& e : it->second.get<picojson::array>())
            if (e.is<std::string>()) out.push_back(e.get<std::string>());
    }
    return out;
}

Weight clampWeight(int w) {
    if (w <= 0) return Weight::Regular;
    if (w < 100) w *= 100;
    if (w > 1000) w = 1000;
    return static_cast<Weight>(w);
}

} // namespace

std::vector<FontEntry> parseFontManifest(std::string_view json, std::string* error) {
    std::vector<FontEntry> out;
    picojson::value root;
    std::string err = picojson::parse(root, std::string(json));
    if (!err.empty()) { if (error) *error = err; return out; }
    if (!root.is<picojson::object>()) {
        if (error) *error = "manifest root is not an object";
        return out;
    }
    const picojson::object& obj = root.get<picojson::object>();
    auto fit = obj.find("fonts");
    if (fit == obj.end() || !fit->second.is<picojson::array>()) {
        if (error) *error = "manifest has no \"fonts\" array";
        return out;
    }

    for (const auto& item : fit->second.get<picojson::array>()) {
        if (!item.is<picojson::object>()) continue;
        const picojson::object& f = item.get<picojson::object>();

        std::string key = getString(f, "file");
        if (key.empty()) key = getString(f, "key");
        if (key.empty()) continue;   // key is required

        FontEntry e;
        e.key = key;
        e.aliases = getStringArray(f, "aliases");

        FontDescriptor& d = e.descriptor;
        d.key = key;
        d.family = getString(f, "family");
        d.subfamily = getString(f, "subfamily");
        d.fullName = getString(f, "fullName");
        d.postScriptName = getString(f, "postScriptName");
        d.typographicFamily = getString(f, "typographicFamily");
        d.scripts = getStringArray(f, "scripts");
        d.languages = getStringArray(f, "languages");

        double num = 0;
        if (getNumber(f, "faceIndex", num)) { e.faceIndex = (int)num; d.faceIndex = (int)num; }
        if (getNumber(f, "weight", num)) { d.weight = clampWeight((int)num); d.styleDeclared = true; }
        if (getNumber(f, "width", num)) {
            int w = (int)num;
            if (w >= 1 && w <= 9) { d.width = static_cast<Width>(w); d.styleDeclared = true; }
        }
        auto iit = f.find("italic");
        if (iit != f.end() && iit->second.is<bool>()) {
            if (iit->second.get<bool>()) d.slant = Slant::Italic;
            d.styleDeclared = true;
        }

        auto flit = f.find("flags");
        if (flit != f.end() && flit->second.is<picojson::array>()) d.styleDeclared = true;
        for (const auto& fl : getStringArray(f, "flags")) {
            if (fl == "color") d.color = true;
            else if (fl == "monospace" || fl == "mono") d.monospace = true;
        }

        auto rit = f.find("ranges");
        if (rit != f.end() && rit->second.is<picojson::array>()) {
            for (const auto& r : rit->second.get<picojson::array>()) {
                if (!r.is<picojson::array>()) continue;
                const auto& pair = r.get<picojson::array>();
                if (pair.size() >= 2 && pair[0].is<double>() && pair[1].is<double>()) {
                    char32_t lo = (char32_t)pair[0].get<double>();
                    char32_t hi = (char32_t)pair[1].get<double>();
                    if (hi < lo) std::swap(lo, hi);
                    d.ranges.push_back({lo, hi});
                }
            }
            std::sort(d.ranges.begin(), d.ranges.end(),
                      [](const CodepointRange& a, const CodepointRange& b) { return a.lo < b.lo; });
        }

        out.push_back(std::move(e));
    }
    return out;
}

int registerFontManifest(Registry& reg, std::string_view json, std::string* error) {
    auto entries = parseFontManifest(json, error);
    int n = 0;
    for (auto& e : entries) { reg.add(std::move(e), false); ++n; }
    return n;
}

} // namespace glyphware
