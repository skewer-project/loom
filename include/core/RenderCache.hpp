#pragma once

#include <unordered_map>
#include <vector>

#include "core/Handle.hpp"
#include "core/Types.hpp"
#include "gpu/ResourceHandles.hpp"

namespace loom::core {

// Key for the RenderCache: (pin, region). Two entries for the same pin at
// different regions are distinct cache lines — a viewport resize naturally
// produces a miss without a separate extent-invalidation pass. Region is
// canonicalised on the way in (RenderCache::store / retrieve) so callers do
// not have to remember the sort order.
struct CacheKey {
    PinHandle pin;
    Region region;

    bool operator==(const CacheKey& other) const {
        return pin == other.pin && region == other.region;
    }
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const noexcept {
        size_t h = std::hash<PinHandle>{}(k.pin);
        h ^= std::hash<Region>{}(k.region) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

class RenderCache {
  public:
    // Stores image for (pin, region). If a previous entry exists for the same
    // key, the old handle is enqueued for release before being overwritten.
    // Without this, a dirty re-eval would leak the previous pool slot every
    // frame.
    void store(PinHandle pin, const Region& region, gpu::ImageHandle image) {
        CacheKey key{pin, region};
        key.region.canonicalize();
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            if (it->second.poolIndex != image.poolIndex ||
                it->second.generation != image.generation) {
                m_pendingImageReleases.push_back(it->second);
            }
            it->second = image;
        } else {
            m_cache.emplace(std::move(key), image);
        }
    }

    gpu::ImageHandle retrieve(PinHandle pin, const Region& region) {
        CacheKey key{pin, region};
        key.region.canonicalize();
        auto it = m_cache.find(key);
        if (it != m_cache.end()) return it->second;
        return {};
    }

    bool hasValidData(Node* node, const Region& region) {
        CacheKey probe{};
        probe.region = region;
        probe.region.canonicalize();
        for (auto outPinHandle : node->outputs) {
            probe.pin = outPinHandle;
            if (m_cache.find(probe) == m_cache.end()) return false;
        }
        return true;
    }

    void evict(PinHandle pin, const Region& region) {
        CacheKey key{pin, region};
        key.region.canonicalize();
        auto it = m_cache.find(key);
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

    // Diagnostic / test access. Not part of the production contract.
    uint32_t DEBUG_size() const { return static_cast<uint32_t>(m_cache.size()); }

  private:
    std::unordered_map<CacheKey, gpu::ImageHandle, CacheKeyHash> m_cache;
    std::vector<gpu::ImageHandle> m_pendingImageReleases;
};

}  // namespace loom::core
