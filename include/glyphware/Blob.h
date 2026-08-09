// glyphware — font byte blob + host-injected loader seam.
//
// glyphware never touches the filesystem or any archive. The host supplies
// font bytes through a FontLoader; glyphware keeps the returned FontBlob alive
// for as long as any Face references it (FreeType memory faces require the
// backing buffer to outlive the face). A host that already has a shared
// on-memory cache (e.g. 吉里吉里Z StorageCache) can hand back blobs that alias
// its cached buffer so no copy is made.
#ifndef GLYPHWARE_BLOB_H
#define GLYPHWARE_BLOB_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace glyphware {

// Immutable, shared, size-carrying font byte buffer. The bytes must remain
// valid and unchanged for the blob's whole lifetime.
class FontBlob {
public:
    virtual ~FontBlob() = default;
    virtual const std::uint8_t* data() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
};

// Convenience blob that owns a heap copy of the bytes.
class OwnedFontBlob final : public FontBlob {
public:
    OwnedFontBlob(const void* src, std::size_t n)
        : bytes_(reinterpret_cast<const std::uint8_t*>(src),
                 reinterpret_cast<const std::uint8_t*>(src) + n) {}
    explicit OwnedFontBlob(std::string bytes) : storage_(std::move(bytes)) {}
    const std::uint8_t* data() const noexcept override {
        return storage_.empty()
            ? bytes_.data()
            : reinterpret_cast<const std::uint8_t*>(storage_.data());
    }
    std::size_t size() const noexcept override {
        return storage_.empty() ? bytes_.size() : storage_.size();
    }
private:
    std::basic_string<std::uint8_t> bytes_;
    std::string storage_;
};

// Host-implemented byte source. `key` is an opaque font identifier chosen by
// the host (a storage path, a logical name, etc.); glyphware treats it only as
// a cache key. Return nullptr when the key is unknown.
class FontLoader {
public:
    virtual ~FontLoader() = default;
    virtual std::shared_ptr<FontBlob> load(std::string_view key) = 0;
};

} // namespace glyphware

#endif // GLYPHWARE_BLOB_H
