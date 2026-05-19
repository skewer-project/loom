#include "gpu/DeepUpload.hpp"

#include <cmath>
#include <cstring>
#include <numeric>

#include "core/AABB.hpp"
#include "core/Assert.hpp"
#include "core/DeepLayout.hpp"
#include "core/Log.hpp"
#include "gpu/LayoutTransitions.hpp"
#include "gpu/StagingArena.hpp"
#include "gpu/TransientBufferPool.hpp"
#include "gpu/TransientImagePool.hpp"
#include "io/ParsedDeepImage.hpp"

namespace loom::gpu {

namespace {

ImageSpec countImageSpec(uint32_t width, uint32_t height) {
    return ImageSpec{VK_FORMAT_R32_UINT,
                     {width, height},
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
}

// Background-sentinel cutoff for Z reduction. Producers (Skewer, others)
// emit `Z = 1e+10` for "no hit" samples — see docs/CONVENTIONS.md §19. We
// also discard non-finite values defensively. `1e9` is well below the
// known sentinel (1e10) and well above any physically plausible scene
// depth in meters, so it catches the sentinel without clipping real data.
constexpr float kBackgroundZCutoff = 1.0e9f;

[[nodiscard]] bool isValidSceneZ(float z) noexcept {
    return std::isfinite(z) && z < kBackgroundZCutoff;
}

}  // namespace

core::AABB reduceSceneBounds(const io::ParsedDeepImage& src, const core::DeepLayout& layout) {
    core::AABB out{};
    const uint64_t totalSamples = src.totalSamples();
    if (totalSamples == 0) return out;

    const int32_t wpxIdx = layout.findChannel("world_pos.x");
    const int32_t wpyIdx = layout.findChannel("world_pos.y");
    const int32_t wpzIdx = layout.findChannel("world_pos.z");
    const bool hasWorldPos = (wpxIdx >= 0 && wpyIdx >= 0 && wpzIdx >= 0);

    auto isFloat32 = [&](int32_t idx) {
        return idx >= 0 &&
               layout.channels()[static_cast<size_t>(idx)].type == core::ChannelType::Float32;
    };

    if (hasWorldPos && isFloat32(wpxIdx) && isFloat32(wpyIdx) && isFloat32(wpzIdx)) {
        const auto* xs =
            reinterpret_cast<const float*>(src.channelData[static_cast<size_t>(wpxIdx)].data());
        const auto* ys =
            reinterpret_cast<const float*>(src.channelData[static_cast<size_t>(wpyIdx)].data());
        const auto* zs =
            reinterpret_cast<const float*>(src.channelData[static_cast<size_t>(wpzIdx)].data());
        for (uint64_t s = 0; s < totalSamples; ++s) {
            const float x = xs[s], y = ys[s], z = zs[s];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) continue;
            // Filter background-sentinel hits using the depth as a proxy.
            // Producers tend to emit the sentinel on _all_ position
            // channels (1e10, 1e10, 1e10), so any one being above the
            // cutoff is enough to flag.
            if (std::abs(x) >= kBackgroundZCutoff || std::abs(y) >= kBackgroundZCutoff ||
                std::abs(z) >= kBackgroundZCutoff) {
                continue;
            }
            out.expand({x, y, z});
        }
        return out;
    }

    // Fallback path. Front-depth-only synthesis matches `PointCloud.vert`
    // at `zScale = 1.0`: XY centered in [-1, 1] from pixel index, Z is
    // negated front depth (RH/Y-up: into-screen is -Z).
    const int32_t zIdx = layout.findChannel("Z");
    if (zIdx < 0 || !isFloat32(zIdx)) {
        if (zIdx < 0) {
            loom::log::info("uploadDeepImage: no 'Z' channel — sceneBounds left invalid");
        } else {
            loom::log::info(
                "uploadDeepImage: 'Z' is not Float32 — sceneBounds left invalid (v1 "
                "skips half-Z reduction)");
        }
        return out;
    }

    const auto* zs =
        reinterpret_cast<const float*>(src.channelData[static_cast<size_t>(zIdx)].data());
    const float invW = (src.width > 0) ? 1.0f / static_cast<float>(src.width) : 0.0f;
    const float invH = (src.height > 0) ? 1.0f / static_cast<float>(src.height) : 0.0f;
    uint64_t cursor = 0;
    const uint64_t pixels = static_cast<uint64_t>(src.width) * src.height;
    for (uint64_t p = 0; p < pixels; ++p) {
        const uint32_t n = src.sampleCounts[p];
        if (n == 0) continue;
        const uint32_t px = static_cast<uint32_t>(p % src.width);
        const uint32_t py = static_cast<uint32_t>(p / src.width);
        const float fx = ((static_cast<float>(px) + 0.5f) * invW) * 2.0f - 1.0f;
        const float fy = 1.0f - ((static_cast<float>(py) + 0.5f) * invH) * 2.0f;
        for (uint32_t s = 0; s < n; ++s, ++cursor) {
            const float z = zs[cursor];
            if (!isValidSceneZ(z)) continue;
            out.expand({fx, fy, -z});
        }
    }
    return out;
}

ResourceRef uploadDeepImage(VkCommandBuffer cmd, StagingArena& staging,
                            TransientImagePool& imagePool, TransientBufferPool& bufferPool,
                            const io::ParsedDeepImage& src, const core::DeepLayout& layout) {
    LOOM_ASSERT(static_cast<uint64_t>(src.width) * src.height == src.sampleCounts.size(),
                "ParsedDeepImage::sampleCounts size mismatches width*height");
    LOOM_ASSERT(src.channelData.size() == layout.channelCount(),
                "ParsedDeepImage::channelData size mismatches layout.channelCount()");

    const uint64_t pixels = static_cast<uint64_t>(src.width) * src.height;
    const uint64_t totalSamples = src.totalSamples();
    const uint32_t stride = layout.stride();

    // Build prefix-sum offsets up front so the offset image upload can run
    // alongside the count upload.
    std::vector<uint32_t> offsets(pixels, 0);
    {
        uint32_t cursor = 0;
        for (size_t i = 0; i < pixels; ++i) {
            offsets[i] = cursor;
            cursor += src.sampleCounts[i];
        }
    }

    // --- Stage count image ---
    StagingArena::Allocation countStaging = staging.allocate(pixels * sizeof(uint32_t), 256);
    if (!countStaging.isValid()) {
        log::error("uploadDeepImage: staging OOM for countImage (", pixels * sizeof(uint32_t),
                   " bytes)");
        return {};
    }
    std::memcpy(countStaging.mapped, src.sampleCounts.data(), pixels * sizeof(uint32_t));

    StagingArena::Allocation offsetStaging = staging.allocate(pixels * sizeof(uint32_t), 256);
    if (!offsetStaging.isValid()) {
        log::error("uploadDeepImage: staging OOM for offsetImage (", pixels * sizeof(uint32_t),
                   " bytes)");
        return {};
    }
    std::memcpy(offsetStaging.mapped, offsets.data(), pixels * sizeof(uint32_t));

    // --- Stage samples buffer: interleave SoA → AoS in staging ---
    const VkDeviceSize sampleBytes = static_cast<VkDeviceSize>(totalSamples) * stride;
    StagingArena::Allocation samplesStaging = staging.allocate(sampleBytes, 16);
    if (!samplesStaging.isValid() && sampleBytes > 0) {
        log::error("uploadDeepImage: staging OOM for samples (", sampleBytes, " bytes)");
        return {};
    }
    if (sampleBytes > 0) {
        auto* dst = static_cast<uint8_t*>(samplesStaging.mapped);
        // Per channel: copy each sample's bytes from src.channelData[ch] to
        // dst + sampleIndex * stride + layout.byteOffset(ch). Total work is
        // O(totalSamples * channelCount); for v1 this is the simplest
        // correct interleave and is comfortably below the bottleneck.
        const auto& channels = layout.channels();
        for (size_t ch = 0; ch < channels.size(); ++ch) {
            const uint32_t off = layout.byteOffset(static_cast<int32_t>(ch));
            const uint32_t size = layout.byteSize(static_cast<int32_t>(ch));
            const auto& srcBytes = src.channelData[ch];
            LOOM_ASSERT(srcBytes.size() == static_cast<size_t>(size) * totalSamples,
                        "channelData byte count mismatches layout.byteSize * totalSamples");
            for (uint64_t s = 0; s < totalSamples; ++s) {
                std::memcpy(dst + s * stride + off, srcBytes.data() + s * size, size);
            }
        }
    }

    // --- Stage sample-to-pixel map (one uint per sample) ---
    const VkDeviceSize sampleToPixelBytes =
        static_cast<VkDeviceSize>(totalSamples) * sizeof(uint32_t);
    StagingArena::Allocation s2pStaging = sampleToPixelBytes > 0
                                              ? staging.allocate(sampleToPixelBytes, 16)
                                              : StagingArena::Allocation{};
    if (!s2pStaging.isValid() && sampleToPixelBytes > 0) {
        log::error("uploadDeepImage: staging OOM for sampleToPixel (", sampleToPixelBytes,
                   " bytes)");
        return {};
    }
    if (sampleToPixelBytes > 0) {
        auto* dst = static_cast<uint32_t*>(s2pStaging.mapped);
        uint64_t cursor = 0;
        for (size_t i = 0; i < pixels; ++i) {
            const uint32_t pi = static_cast<uint32_t>(i);
            for (uint32_t s = 0; s < src.sampleCounts[i]; ++s) {
                dst[cursor++] = pi;
            }
        }
    }

    // --- Acquire GPU resources ---
    ImageHandle countImage = imagePool.acquire(countImageSpec(src.width, src.height));
    ImageHandle offsetImage = imagePool.acquire(countImageSpec(src.width, src.height));
    BufferHandle samples = sampleBytes > 0 ? bufferPool.acquire(sampleBytes) : BufferHandle{};
    BufferHandle sampleToPixel =
        sampleToPixelBytes > 0 ? bufferPool.acquire(sampleToPixelBytes) : BufferHandle{};

    if (!countImage.isValid() || !offsetImage.isValid()) {
        log::error("uploadDeepImage: pool acquire failed");
        return {};
    }

    // Transition images UNDEFINED → TRANSFER_DST_OPTIMAL before copy.
    transitionImageLayout(cmd, imagePool.getImage(countImage), VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    transitionImageLayout(cmd, imagePool.getImage(offsetImage), VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    auto issueImageCopy = [&](StagingArena::Allocation a, ImageHandle img) {
        VkBufferImageCopy region{};
        region.bufferOffset = a.offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {src.width, src.height, 1};
        vkCmdCopyBufferToImage(cmd, a.buffer, imagePool.getImage(img),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    };
    issueImageCopy(countStaging, countImage);
    issueImageCopy(offsetStaging, offsetImage);

    // Transition images to GENERAL — the layout the rest of the engine
    // expects for transient compute resources (see docs/CONVENTIONS.md §3).
    transitionImageLayout(cmd, imagePool.getImage(countImage), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_GENERAL);
    transitionImageLayout(cmd, imagePool.getImage(offsetImage),
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    imagePool.setLayout(countImage, VK_IMAGE_LAYOUT_GENERAL);
    imagePool.setLayout(offsetImage, VK_IMAGE_LAYOUT_GENERAL);

    std::vector<VkBufferMemoryBarrier2> bufBarriers;
    auto issueBufferCopy = [&](StagingArena::Allocation a, BufferHandle dst, VkDeviceSize size) {
        if (!dst.isValid() || size == 0) return;
        VkBufferCopy copy{};
        copy.srcOffset = a.offset;
        copy.dstOffset = 0;
        copy.size = size;
        vkCmdCopyBuffer(cmd, a.buffer, bufferPool.getBuffer(dst), 1, &copy);

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        // Both compute and vertex stages consume these buffers (compute for
        // DeepFlatten, vertex for PointCloudPass). Promote to ALL_COMMANDS
        // covers both without a second barrier emission.
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.buffer = bufferPool.getBuffer(dst);
        barrier.offset = 0;
        barrier.size = size;
        bufBarriers.push_back(barrier);
    };
    issueBufferCopy(samplesStaging, samples, sampleBytes);
    issueBufferCopy(s2pStaging, sampleToPixel, sampleToPixelBytes);

    if (!bufBarriers.empty()) {
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = static_cast<uint32_t>(bufBarriers.size());
        dep.pBufferMemoryBarriers = bufBarriers.data();
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    ResourceRef ref;
    ref.kind = ResourceRef::Kind::Deep;
    ref.deep.countImage = countImage;
    ref.deep.offsetImage = offsetImage;
    ref.deep.samples = samples;
    ref.deep.sampleToPixel = sampleToPixel;
    ref.deep.layout = &layout;
    ref.deep.width = src.width;
    ref.deep.height = src.height;
    ref.deep.totalSamples = totalSamples;
    // Bounds reduction runs CPU-side on the same SoA data the interleave
    // pass already touched — no extra GPU work. Folded into the upload
    // path (rather than computed later from the device buffer) because
    // host-side access is free here and the alternative is a GPU readback.
    ref.deep.sceneBounds = reduceSceneBounds(src, layout);
    return ref;
}

}  // namespace loom::gpu
