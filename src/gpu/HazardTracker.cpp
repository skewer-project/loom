#include "gpu/HazardTracker.hpp"

#include "gpu/ComputeTask.hpp"

namespace loom::gpu {

namespace {

ImageKey toKey(const ImageHandle& h) noexcept { return ImageKey{h.poolIndex, h.generation}; }

}  // namespace

void HazardTracker::beginFrame() {
    m_writtenKeys.clear();
    m_readKeys.clear();
}

bool HazardTracker::needsBarrierBeforeRead(std::span<const ImageHandle> reads) const {
    for (const auto& h : reads) {
        if (!h.isValid()) continue;
        if (m_writtenKeys.contains(toKey(h))) return true;
    }
    return false;
}

bool HazardTracker::needsBarrierBeforeWrite(std::span<const ImageHandle> writes) const {
    for (const auto& h : writes) {
        if (!h.isValid()) continue;
        if (m_writtenKeys.contains(toKey(h))) return true;
    }
    return false;
}

bool HazardTracker::needsBarrierBeforeWriteAfterRead(
    std::span<const ImageHandle> /*writes*/) const {
    // WAR scaffold. Read set is tracked via recordTask but no node in v1 reads
    // then writes the same slot inside a frame. When that changes, the body
    // becomes:
    //   for (h : writes) if (m_readKeys.contains(toKey(h))) return true;
    return false;
}

void HazardTracker::recordTask(const ComputeTask& task) {
    for (const auto& h : task.readDependencies) {
        if (h.isValid()) m_readKeys.insert(toKey(h));
    }
    for (const auto& h : task.writeDependencies) {
        if (h.isValid()) m_writtenKeys.insert(toKey(h));
    }
}

void HazardTracker::clearAfterBarrier() {
    // A pipeline memory barrier subsumes every prior access of the affected
    // memory. After it executes, no previously-recorded hazard remains.
    m_writtenKeys.clear();
    m_readKeys.clear();
}

}  // namespace loom::gpu
