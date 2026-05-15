#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <unordered_set>

#include "gpu/ResourceHandles.hpp"

namespace loom::gpu {

struct ComputeTask;

// HazardTracker is the single source of truth for inter-dispatch hazards
// inside DispatchManager. It tracks which (kind, poolIndex, generation)
// resource keys have been written or read since the last barrier and answers
// queries about whether a barrier is required before the next read or write.
//
// Key shape: ResourceKey is (kind, poolIndex, generation), where kind is
// `Image` or `Buffer`. Keying on the generation makes the deferred-release
// lifetime contract explicit by construction — a handle that has been
// released and re-acquired bumps its generation, so the new use does not
// collide with the previous use in the hazard sets. Keying additionally on
// kind prevents an image's `(poolIndex, generation)` from colliding with a
// buffer's `(poolIndex, generation)` (the two pools are independent
// numbering spaces). Both safeguards matter once deep-EXR payloads land:
// every deep payload is three resources (two images, one buffer), and the
// tracker must distinguish them.
//
// Hazard coverage:
//   - RAW (read after write): emit a SHADER_WRITE -> SHADER_READ barrier.
//   - WAW (write after write): emit a SHADER_WRITE -> SHADER_WRITE barrier.
//   - WAR (write after read): scaffolded — the read set is maintained but
//     needsBarrierBeforeWriteAfterRead always returns false today. Flip the
//     return when the first node reads-then-writes the same slot inside a
//     frame. The plumbing exists so that change is one line, not a refactor.

struct ResourceKey {
    enum class Kind : uint8_t { Image, Buffer };
    Kind kind;
    uint32_t poolIndex;
    uint32_t generation;

    bool operator==(const ResourceKey& other) const noexcept {
        return kind == other.kind && poolIndex == other.poolIndex && generation == other.generation;
    }
};

struct ResourceKeyHash {
    size_t operator()(const ResourceKey& k) const noexcept {
        size_t h = (static_cast<size_t>(k.generation) << 32) ^ static_cast<size_t>(k.poolIndex);
        h ^= static_cast<size_t>(k.kind) * 0x9E3779B97F4A7C15ULL;
        return h;
    }
};

class HazardTracker {
  public:
    // Clear all tracking state. Called at the start of each frame's submit().
    void beginFrame();

    // Returns true if any of `reads` (or `readBuffers`) was previously
    // recorded as a write since the last barrier. Caller is expected to
    // emit a memory barrier and call clearAfterBarrier(). The buffer span
    // defaults to empty so existing image-only call sites compile
    // unchanged.
    bool needsBarrierBeforeRead(std::span<const ImageHandle> reads,
                                std::span<const BufferHandle> readBuffers = {}) const;

    // Returns true if any of `writes` (or `writeBuffers`) was previously
    // recorded as a write since the last barrier (WAW hazard).
    bool needsBarrierBeforeWrite(std::span<const ImageHandle> writes,
                                 std::span<const BufferHandle> writeBuffers = {}) const;

    // WAR scaffold. Always returns false in v1 — no current node reads-then-
    // writes the same slot inside a frame. Read tracking is maintained so
    // flipping this on is a one-line change when a node needs it.
    bool needsBarrierBeforeWriteAfterRead(std::span<const ImageHandle> writes,
                                          std::span<const BufferHandle> writeBuffers = {}) const;

    // Record the task's reads and writes (both images and buffers). Call
    // after handling barriers.
    void recordTask(const ComputeTask& task);

    // After a memory barrier the previous hazards are all resolved — the
    // barrier subsumes them. Clear the tracking sets so the next task starts
    // fresh.
    void clearAfterBarrier();

  private:
    std::unordered_set<ResourceKey, ResourceKeyHash> m_writtenKeys;
    std::unordered_set<ResourceKey, ResourceKeyHash> m_readKeys;
};

}  // namespace loom::gpu
