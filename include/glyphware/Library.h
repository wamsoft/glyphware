// glyphware — the shared FreeType library handle.
//
// A single FT_Library is shared by every Face. It is reference-counted through
// std::shared_ptr and torn down only when the last owner (Library or Face)
// drops — deliberately unlike 吉里吉里Z's current core, where an instance
// destructor calls FT_Done_FreeType on a global and can pull the library out
// from under other users. Access is mutex-guarded (FreeType is not thread-safe
// for concurrent use of one FT_Library).
#ifndef GLYPHWARE_LIBRARY_H
#define GLYPHWARE_LIBRARY_H

#include <memory>
#include <mutex>

// Opaque FreeType handle, kept out of the public header.
typedef struct FT_LibraryRec_* FT_Library;

namespace glyphware {

class Library {
public:
    // Returns the process-wide shared library, creating it on first use.
    // Hold the returned shared_ptr (Face does) to keep FreeType alive.
    static std::shared_ptr<Library> instance();

    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    FT_Library ft() const noexcept { return ft_; }
    std::mutex& mutex() noexcept { return mutex_; }

    // Runtime FreeType version, for diagnostics.
    void version(int& major, int& minor, int& patch) const;

protected:
    Library();
private:
    FT_Library ft_ = nullptr;
    mutable std::mutex mutex_;
};

// RAII lock over the shared library's mutex; use around any FT_* call sequence.
class LibraryLock {
public:
    explicit LibraryLock(Library& lib) : lock_(lib.mutex()) {}
private:
    std::lock_guard<std::mutex> lock_;
};

} // namespace glyphware

#endif // GLYPHWARE_LIBRARY_H
