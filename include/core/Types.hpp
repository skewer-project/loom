#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/Handle.hpp"
#include "gpu/ResourceHandles.hpp"

namespace loom::core {

class Graph;
struct EvaluationContext;

enum class IdTag : uint64_t { Node = 0ULL, Pin = 1ULL << 62, Link = 2ULL << 62 };

inline uint64_t encodeId(uint32_t index, uint32_t generation, IdTag tag) {
    // Pack into 64 bits: [Tag: 2][Gen: 30][Index: 32]
    return (uint64_t)tag | ((uint64_t)(generation & 0x3FFFFFFF) << 32) | (uint64_t)(index + 1);
}

inline uint32_t decodeIndex(uint64_t id) {
    // Simply extract the bottom 32 bits and revert the +1 offset
    return (uint32_t)(id & 0xFFFFFFFF) - 1;
}

enum class PinDirection { Input, Output };
enum class PinType { Float, DeepBuffer };
enum class NodeType { Constant, Merge, Viewer, Passthrough };

struct Tile {
    uint32_t x, y;
    uint32_t width, height;

    bool operator==(const Tile& other) const {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }

    bool operator!=(const Tile& other) const { return !(*this == other); }
};

struct Region {
    std::vector<Tile> tiles;

    bool operator==(const Region& other) const { return tiles == other.tiles; }
    bool operator!=(const Region& other) const { return !(*this == other); }

    // Sort tiles by (y, x) so that two regions with the same tile set in
    // different insertion order hash and compare equal. The cache canonicalises
    // on the way in (see RenderCache), so callers that build regions tile-by-
    // tile do not need to remember to sort.
    void canonicalize() {
        std::sort(tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) {
            if (a.y != b.y) return a.y < b.y;
            if (a.x != b.x) return a.x < b.x;
            if (a.height != b.height) return a.height < b.height;
            return a.width < b.width;
        });
    }
};

struct Pin {
    PinHandle id;
    NodeHandle node;  // Owner
    PinDirection direction;
    PinType type;
    LinkHandle link;                // For Inputs: holds at most one link. Default to invalid.
    std::vector<LinkHandle> links;  // For Outputs: holds multiple outbound links.

    Pin(PinHandle h, NodeHandle n, PinDirection dir, PinType t)
        : id(h), node(n), direction(dir), type(t), link(), links() {}
};

struct Link {
    LinkHandle id;
    PinHandle startPin;  // Must be an Output pin
    PinHandle endPin;    // Must be an Input pin

    Link(LinkHandle h, PinHandle start, PinHandle end) : id(h), startPin(start), endPin(end) {}
};

struct Node {
    NodeHandle id;
    NodeType type;
    std::string name;
    std::vector<PinHandle> inputs;
    std::vector<PinHandle> outputs;
    bool isDirty = true;
    Graph* graph = nullptr;

    Node(NodeHandle h, NodeType t, std::string n)
        : id(h), type(t), name(std::move(n)), inputs(), outputs(), isDirty(true), graph(nullptr) {}

    virtual ~Node() = default;

    // Pass 1: Mark required tiles and collect active nodes
    virtual void markRequiredTiles(const Region& requestedRegion,
                                   std::unordered_set<NodeHandle>& activeNodes) = 0;

    // Pass 2: Record Vulkan compute commands for the specified region
    virtual void execute(EvaluationContext& ctx, const Region& region) = 0;

  protected:
    gpu::ImageHandle pullInput(EvaluationContext& ctx, uint32_t inputIndex);
};

}  // namespace loom::core

namespace std {
template <>
struct hash<loom::core::Tile> {
    size_t operator()(const loom::core::Tile& t) const noexcept {
        // Mix the four uint32 fields. The constants are arbitrary primes —
        // the goal is to scramble lattice patterns (axis-aligned tile grids
        // are the common case) rather than maximise crypto-strength entropy.
        uint64_t h = (uint64_t)t.x;
        h = h * 0x9E3779B97F4A7C15ULL + (uint64_t)t.y;
        h = h * 0x9E3779B97F4A7C15ULL + (uint64_t)t.width;
        h = h * 0x9E3779B97F4A7C15ULL + (uint64_t)t.height;
        return hash<uint64_t>{}(h);
    }
};

template <>
struct hash<loom::core::Region> {
    size_t operator()(const loom::core::Region& r) const noexcept {
        // Order-dependent: callers must canonicalize() before hashing if two
        // regions with the same tile set in different orders must collide.
        // RenderCache canonicalises internally before keying.
        size_t h = 0;
        hash<loom::core::Tile> tileHash;
        for (const auto& t : r.tiles) {
            h ^= tileHash(t) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        }
        return h;
    }
};
}  // namespace std
