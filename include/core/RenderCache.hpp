#pragma once

#include <unordered_map>
#include <vector>

#include "core/Handle.hpp"
#include "core/Types.hpp"
#include "gpu/ResourceHandles.hpp"

namespace loom::core {

class RenderCache {
  public:
    void store(PinHandle pin, const Region& region, gpu::ImageHandle image) {
        m_cache[pinKey(pin)] = image;
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

    std::vector<gpu::ImageHandle> takePendingReleases() {
        std::vector<gpu::ImageHandle> result = std::move(m_pendingImageReleases);
        m_pendingImageReleases.clear();
        return result;
    }

    void garbageCollect(const Graph* graph);

  private:
    uint64_t pinKey(PinHandle h) const { return ((uint64_t)h.generation << 32) | h.index; }

    std::unordered_map<uint64_t, gpu::ImageHandle> m_cache;
    std::vector<gpu::ImageHandle> m_pendingImageReleases;
};

}  // namespace loom::core
