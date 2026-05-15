#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

#include "vk_mem_alloc.h"

namespace loom::gpu {

// Host-visible, mapped bump arena backed by a single `VkBuffer`. Used as the
// CPU-side staging surface for one-shot uploads to device-local resources
// (deep-EXR samples, count/offset images, future texture / vertex uploads).
//
// Lifetime model
// --------------
//
// V1 is a bump allocator with explicit `reset()` rather than a true ring.
// The intended call pattern is:
//
//   - `allocate(size)` from anywhere on the main thread until the arena
//     fills up or the upload batch is complete.
//   - Record the corresponding `vkCmdCopy*` calls on a command buffer
//     and submit.
//   - Once the GPU is done with those copies (typically two frames later,
//     gated by `FrameLoop::onFrameRetired`), call `reset()` to reclaim
//     the entire arena.
//
// The simplification (whole-arena reset instead of per-allocation tagging)
// is fine because v1 has no async upload path — every staging allocation
// in a frame is retired together once that frame's submit retires. The
// Phase C.1 worker-thread upload will need finer-grain lifetime tracking
// (or a second arena per worker), but that's a future-PR concern.
//
// Per-frame upload budget
// -----------------------
//
// The arena is sized at construction. A 16 MB default holds ~262k packed
// samples at the 64-byte NVS layout (3-vec3 + RGBA + Z), or one ~1080p
// count/offset image-pair. Animation playback (Phase C) sets the capacity
// from a CLI / config knob and uses an LRU over `RenderCache` entries
// rather than over arena slots — the arena is per-frame scratch, not the
// long-lived store.
//
// LRU strategy for animation playback (Phase C)
// ---------------------------------------------
//
// `RenderCache` already keys on `(pin, region)`; the playback path
// constructs a unique pin per (file, frame_index) so cache eviction
// drops the oldest frame's deep buffers as a side effect of new
// uploads. The arena itself does not implement LRU — it's per-frame
// scratch. The cache eviction does the heavy lifting; the arena merely
// ensures each frame's upload fits in a known-bounded host buffer.
class StagingArena {
  public:
    // Sub-allocation inside the arena. `mapped` is a pointer into the
    // arena's persistently-mapped host memory; the caller writes the
    // payload bytes through this pointer and then records a `vkCmdCopy*`
    // call referencing `{buffer, offset, size}`. The pointer is valid
    // until the next `reset()`.
    struct Allocation {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        void* mapped = nullptr;

        [[nodiscard]] bool isValid() const { return buffer != VK_NULL_HANDLE && size > 0; }
    };

    // Default 16 MiB. Production callers explicitly size up for known
    // workloads (e.g., 256 MiB for a deep-EXR playback session).
    static constexpr VkDeviceSize kDefaultCapacityBytes = 16ULL * 1024 * 1024;

    StagingArena(VmaAllocator allocator, VkDeviceSize capacity = kDefaultCapacityBytes);
    ~StagingArena();

    StagingArena(const StagingArena&) = delete;
    StagingArena& operator=(const StagingArena&) = delete;

    // Sub-allocate `size` bytes from the bump pointer, rounded up to
    // `alignment`. Returns an invalid `Allocation` on OOM (size > free).
    // The default 16-byte alignment matches `vkCmdCopyBuffer`'s natural
    // alignment requirement and is wide enough for the
    // `optimalBufferCopyOffsetAlignment` minimum reported by every desktop
    // driver we care about; 256-byte alignment is required for some
    // image-copy targets — pass it explicitly when staging to an image.
    [[nodiscard]] Allocation allocate(VkDeviceSize size, VkDeviceSize alignment = 16);

    // Reset the bump pointer to zero. ONLY safe to call after the GPU has
    // finished consuming every allocation handed out since the previous
    // `reset` — typically gated by `FrameLoop::onFrameRetired` plus
    // `MAX_FRAMES_IN_FLIGHT` margin.
    void reset();

    [[nodiscard]] VkDeviceSize capacity() const { return m_capacity; }
    [[nodiscard]] VkDeviceSize used() const { return m_head; }
    [[nodiscard]] VkBuffer buffer() const { return m_buffer; }

  private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    void* m_mapped = nullptr;
    VkDeviceSize m_capacity = 0;
    VkDeviceSize m_head = 0;
};

}  // namespace loom::gpu
