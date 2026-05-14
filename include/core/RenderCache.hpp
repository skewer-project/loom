#pragma once

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

#include "core/Handle.hpp"
#include "core/Types.hpp"
#include "gpu/ResourceHandles.hpp"

namespace loom::core {

class RenderCache {
  public:
    // Stores image for (pin, region). If a previous entry exists for the same
    // key, the old handle is enqueued for release before being overwritten.
    // Without this, a dirty re-eval would leak the previous pool slot every
    // frame.
    void store(PinHandle pin, const Region& region, gpu::ImageHandle image) {
        const uint64_t key = pinKey(pin);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            if (it->second.poolIndex != image.poolIndex ||
                it->second.generation != image.generation) {
                m_pendingImageReleases.push_back(it->second);
            }
            it->second = image;
        } else {
            m_cache.emplace(key, image);
        }
    }

    gpu::ImageHandle retrieve(PinHandle pin, const Region& region) {
        auto it = m_cache.find(pinKey(pin));
        if (it != m_cache.end()) return it->second;
        return {};
    }

    bool hasValidData(Node* node, const Region& region) {
        for (auto outPinHandle : node->outputs) {
            if (m_cache.find(pinKey(outPinHandle)) == m_cache.end()) return false;
        }
        return true;
    }

    void evict(PinHandle pin) {
        auto it = m_cache.find(pinKey(pin));
        if (it != m_cache.end()) {
            m_pendingImageReleases.push_back(it->second);
            m_cache.erase(it);
        }
    }

    void clear() {
        for (auto& pair : m_cache) {
            m_pendingImageReleases.push_back(pair.second);
        }
        m_cache.clear();
    }

    // Interim solution before region keying lands (Phase 4): when the
    // evaluator's requested extent changes, every cached entry is at the wrong
    // resolution and must be released. Returns true if an invalidation was
    // performed. Once Phase 4 introduces (pin, region) keying, the region
    // change naturally produces a cache miss and this helper is removed.
    bool invalidateIfExtentChanged(VkExtent2D extent) {
        if (extent.width == m_lastExtent.width && extent.height == m_lastExtent.height) {
            return false;
        }
        m_lastExtent = extent;
        if (!m_cache.empty()) {
            clear();
            return true;
        }
        return false;
    }

    std::vector<gpu::ImageHandle> takePendingReleases() {
        std::vector<gpu::ImageHandle> result = std::move(m_pendingImageReleases);
        m_pendingImageReleases.clear();
        return result;
    }

    void garbageCollect(const Graph* graph);

    // Diagnostic / test access. Not part of the production contract.
    uint32_t DEBUG_size() const { return static_cast<uint32_t>(m_cache.size()); }

  private:
    uint64_t pinKey(PinHandle h) const { return ((uint64_t)h.generation << 32) | h.index; }

    std::unordered_map<uint64_t, gpu::ImageHandle> m_cache;
    std::vector<gpu::ImageHandle> m_pendingImageReleases;
    VkExtent2D m_lastExtent{0, 0};
};

}  // namespace loom::core
