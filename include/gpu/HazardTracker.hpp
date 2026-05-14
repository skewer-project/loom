#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <unordered_set>

#include "gpu/ResourceHandles.hpp"

namespace loom::gpu {

struct ComputeTask;

// HazardTracker is the single source of truth for inter-dispatch hazards
// inside DispatchManager. It tracks which (poolIndex, generation) image keys
// have been written or read since the last barrier and answers queries about
// whether a barrier is required before the next read or write.
//
// Key shape: ImageKey is (poolIndex, generation). Keying on the generation
// makes the deferred-release lifetime contract explicit by construction — an
// ImageHandle that has been released and re-acquired bumps its generation,
// so the new use does not collide with the previous use in the hazard sets.
// This is safer than keying on bindlessSlot, where slot recycling could mask
// a real hazard.
//
// Hazard coverage:
//   - RAW (read after write): emit a SHADER_WRITE -> SHADER_READ barrier.
//   - WAW (write after write): emit a SHADER_WRITE -> SHADER_WRITE barrier.
//   - WAR (write after read): scaffolded — the read set is maintained but
//     needsBarrierBeforeWriteAfterRead always returns false today. Flip the
//     return when the first node reads-then-writes the same slot inside a
//     frame. The plumbing exists so that change is one line, not a refactor.

struct ImageKey {
    uint32_t poolIndex;
    uint32_t generation;

    bool operator==(const ImageKey& other) const noexcept {
        return poolIndex == other.poolIndex && generation == other.generation;
    }
};

struct ImageKeyHash {
    size_t operator()(const ImageKey& k) const noexcept {
        return (static_cast<size_t>(k.generation) << 32) ^ static_cast<size_t>(k.poolIndex);
    }
};

class HazardTracker {
  public:
    // Clear all tracking state. Called at the start of each frame's submit().
    void beginFrame();

    // Returns true if any of `reads` was previously recorded as a write since
    // the last barrier. Caller is expected to emit a memory barrier and call
    // clearAfterBarrier().
    bool needsBarrierBeforeRead(std::span<const ImageHandle> reads) const;

    // Returns true if any of `writes` was previously recorded as a write
    // since the last barrier (WAW hazard).
    bool needsBarrierBeforeWrite(std::span<const ImageHandle> writes) const;

    // WAR scaffold. Always returns false in v1 — no current node reads-then-
    // writes the same slot inside a frame. Read tracking is maintained so
    // flipping this on is a one-line change when a node needs it.
    bool needsBarrierBeforeWriteAfterRead(std::span<const ImageHandle> writes) const;

    // Record the task's reads and writes. Call after handling barriers.
    void recordTask(const ComputeTask& task);

    // After a memory barrier the previous hazards are all resolved — the
    // barrier subsumes them. Clear the tracking sets so the next task starts
    // fresh.
    void clearAfterBarrier();

  private:
    std::unordered_set<ImageKey, ImageKeyHash> m_writtenKeys;
    std::unordered_set<ImageKey, ImageKeyHash> m_readKeys;
};

}  // namespace loom::gpu
