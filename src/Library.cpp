#include "glyphware/Library.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdexcept>

namespace glyphware {

Library::Library() {
    FT_Error err = FT_Init_FreeType(&ft_);
    if (err != 0 || ft_ == nullptr) {
        throw std::runtime_error("glyphware: FT_Init_FreeType failed");
    }
}

Library::~Library() {
    if (ft_) {
        FT_Done_FreeType(ft_);
        ft_ = nullptr;
    }
}

void Library::version(int& major, int& minor, int& patch) const {
    FT_Int a = 0, b = 0, c = 0;
    FT_Library_Version(ft_, &a, &b, &c);
    major = a; minor = b; patch = c;
}

std::shared_ptr<Library> Library::instance() {
    // Process-wide shared library, created on first use and destroyed only when
    // the last Face/Library owner drops. Guarded so concurrent first-uses race
    // safely. std::shared_ptr<Library>(new Library) — ctor is private, so use a
    // small struct that can call it.
    static std::mutex mtx;
    static std::weak_ptr<Library> weak;

    std::lock_guard<std::mutex> lock(mtx);
    if (auto sp = weak.lock()) {
        return sp;
    }
    struct Access : Library {};
    auto sp = std::make_shared<Access>();
    weak = sp;
    return sp;
}

} // namespace glyphware
