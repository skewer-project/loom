#include "core/RenderCache.hpp"

#include "core/Graph.hpp"

namespace loom::core {

void RenderCache::garbageCollect(const Graph* graph) {
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (!graph->getPin(it->first.pin)) {
            if (it->second.kind == gpu::ResourceRef::Kind::Image) {
                m_pendingImageReleases.push_back(it->second.image);
            }
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace loom::core
