#pragma once
// Stable IDs: strongly typed, monotonically allocated, never reused within a
// world. Saves, undo and the Unreal side all refer to objects by these.
#include <compare>
#include <cstdint>
#include <functional>

namespace cp {

template <typename Tag>
struct Id {
    std::uint64_t value = 0;  // 0 is the null ID

    constexpr bool valid() const { return value != 0; }
    constexpr explicit operator bool() const { return valid(); }
    friend constexpr auto operator<=>(Id, Id) = default;
};

// Hands out IDs for one object kind. Undoing a create does not roll the
// counter back, so a redo or a later create never collides with a stale ref.
template <typename Tag>
class IdAllocator {
public:
    Id<Tag> next() { return Id<Tag>{++last_}; }
    std::uint64_t last() const { return last_; }
    void reserve_through(std::uint64_t v) { if (v > last_) last_ = v; }

private:
    std::uint64_t last_ = 0;
};

}  // namespace cp

template <typename Tag>
struct std::hash<cp::Id<Tag>> {
    std::size_t operator()(cp::Id<Tag> id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
