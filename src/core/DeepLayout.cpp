#include "core/DeepLayout.hpp"

#include <memory>
#include <unordered_map>

namespace loom::core {

namespace {

// Bucketed registry: hash → vector of layouts that share that hash. On a hit
// we compare channel-lists to disambiguate collisions. With the natural
// channel-sequence distribution this is effectively flat (collisions are rare
// at the byte-content level), but the bucket fallback makes the contract
// robust against pathological hash collisions.
struct Registry {
    std::unordered_map<size_t, std::vector<std::unique_ptr<DeepLayout>>> bucketsByHash;
    size_t totalCount = 0;
};

Registry& registry() {
    static Registry r;
    return r;
}

}  // namespace

DeepLayout::DeepLayout(std::vector<DeepChannel> channels) : m_channels(std::move(channels)) {
    m_offsets.reserve(m_channels.size());
    uint32_t cursor = 0;
    size_t h = 0xcbf29ce484222325ULL;  // FNV-1a seed
    for (const auto& c : m_channels) {
        m_offsets.push_back(cursor);
        cursor += channelTypeBytes(c.type) * c.components;
        // boost::hash_combine pattern. The golden-ratio constant scrambles
        // the running hash so position within the channel list matters
        // (different orderings hash differently).
        h ^= std::hash<DeepChannel>{}(c) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    }
    m_stride = cursor;
    m_hash = h;
}

int32_t DeepLayout::findChannel(std::string_view name) const noexcept {
    for (size_t i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i].name == name) return static_cast<int32_t>(i);
    }
    return -1;
}

uint32_t DeepLayout::byteOffset(std::string_view name) const noexcept {
    int32_t idx = findChannel(name);
    if (idx < 0) return kInvalidOffset;
    return m_offsets[static_cast<size_t>(idx)];
}

uint32_t DeepLayout::byteOffset(int32_t channelIndex) const noexcept {
    if (channelIndex < 0 || static_cast<size_t>(channelIndex) >= m_offsets.size()) {
        return kInvalidOffset;
    }
    return m_offsets[static_cast<size_t>(channelIndex)];
}

uint32_t DeepLayout::byteSize(int32_t channelIndex) const noexcept {
    if (channelIndex < 0 || static_cast<size_t>(channelIndex) >= m_channels.size()) return 0;
    const auto& c = m_channels[static_cast<size_t>(channelIndex)];
    return channelTypeBytes(c.type) * c.components;
}

const DeepLayout* getDeepLayout(std::vector<DeepChannel> channels) {
    // Build a candidate first so the hash is computed exactly the same way
    // for lookup and for storage. The candidate is discarded on a hit.
    auto candidate = std::make_unique<DeepLayout>(std::move(channels));
    const size_t h = candidate->hash();

    auto& reg = registry();
    auto& bucket = reg.bucketsByHash[h];
    for (const auto& existing : bucket) {
        if (existing->channelCount() != candidate->channelCount()) continue;
        bool same = true;
        for (size_t i = 0; i < existing->channelCount(); ++i) {
            if (existing->channels()[i] != candidate->channels()[i]) {
                same = false;
                break;
            }
        }
        if (same) return existing.get();
    }

    const DeepLayout* raw = candidate.get();
    bucket.push_back(std::move(candidate));
    ++reg.totalCount;
    return raw;
}

size_t DEBUG_internedLayoutCount() { return registry().totalCount; }

void DEBUG_clearLayoutRegistry() {
    auto& r = registry();
    r.bucketsByHash.clear();
    r.totalCount = 0;
}

}  // namespace loom::core

namespace std {

size_t hash<loom::core::DeepChannel>::operator()(const loom::core::DeepChannel& c) const noexcept {
    size_t h = hash<string>{}(c.name);
    h ^=
        hash<uint8_t>{}(static_cast<uint8_t>(c.type)) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    h ^= hash<uint8_t>{}(c.components) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return h;
}

}  // namespace std
