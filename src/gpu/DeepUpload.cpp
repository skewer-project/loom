#include "gpu/DeepUpload.hpp"

#include <cstring>
#include <numeric>

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

}  // namespace

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

    // --- Acquire GPU resources ---
    ImageHandle countImage = imagePool.acquire(countImageSpec(src.width, src.height));
    ImageHandle offsetImage = imagePool.acquire(countImageSpec(src.width, src.height));
    BufferHandle samples = sampleBytes > 0 ? bufferPool.acquire(sampleBytes) : BufferHandle{};

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

    if (samples.isValid()) {
        VkBufferCopy copy{};
        copy.srcOffset = samplesStaging.offset;
        copy.dstOffset = 0;
        copy.size = sampleBytes;
        vkCmdCopyBuffer(cmd, samplesStaging.buffer, bufferPool.getBuffer(samples), 1, &copy);

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.buffer = bufferPool.getBuffer(samples);
        barrier.offset = 0;
        barrier.size = sampleBytes;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    ResourceRef ref;
    ref.kind = ResourceRef::Kind::Deep;
    ref.deep.countImage = countImage;
    ref.deep.offsetImage = offsetImage;
    ref.deep.samples = samples;
    ref.deep.layout = &layout;
    ref.deep.width = src.width;
    ref.deep.height = src.height;
    return ref;
}

}  // namespace loom::gpu
