#include "core/RenderCache.hpp"

#include "core/Graph.hpp"

namespace loom::core {

void RenderCache::garbageCollect(const Graph* graph) {
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        uint64_t key = it->first;
        PinHandle pinHandle;
        pinHandle.index = (uint32_t)(key & 0xFFFFFFFF);
        pinHandle.generation = (uint32_t)(key >> 32);

        if (!graph->getPin(pinHandle)) {
            m_pendingImageReleases.push_back(it->second);
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace loom::core
