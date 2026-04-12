#pragma once

#include <cstdint>
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
