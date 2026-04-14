#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace loom::core {

template <typename Tag>
struct Handle {
    uint32_t index;
    uint32_t generation;

    constexpr Handle() noexcept : index(std::numeric_limits<uint32_t>::max()), generation(0) {}

    constexpr Handle(uint32_t idx, uint32_t gen) noexcept : index(idx), generation(gen) {}

    constexpr bool operator==(const Handle& other) const noexcept {
        return index == other.index && generation == other.generation;
    }

    constexpr bool operator!=(const Handle& other) const noexcept { return !(*this == other); }

    constexpr bool isValid() const noexcept {
        return index != std::numeric_limits<uint32_t>::max() && generation != 0;
    }
};

struct NodeTag {};
using NodeHandle = Handle<NodeTag>;

struct PinTag {};
using PinHandle = Handle<PinTag>;

struct LinkTag {};
using LinkHandle = Handle<LinkTag>;

}  // namespace loom::core

namespace std {
template <typename Tag>
struct hash<loom::core::Handle<Tag>> {
    size_t operator()(const loom::core::Handle<Tag>& h) const noexcept {
        return hash<uint64_t>{}((static_cast<uint64_t>(h.generation) << 32) | h.index);
    }
};
}  // namespace std
