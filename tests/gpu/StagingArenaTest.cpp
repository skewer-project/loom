#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "core/DeepLayout.hpp"
#include "gpu/DeepUpload.hpp"
#include "gpu/StagingArena.hpp"
#include "gpu/TransientBufferPool.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"
#include "io/ParsedDeepImage.hpp"
#include "platform/Window.hpp"

namespace core = loom::core;
namespace gpu = loom::gpu;
namespace io = loom::io;
namespace platform = loom::platform;

class StagingArenaTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        try {
            if (!glfwInit()) return;
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            window = std::make_unique<platform::Window>(100, 100, "Test");
            ctx = std::make_unique<gpu::VulkanContext>();
            ctx->init(*window, "StagingArenaTest");
            m_initialized = true;
        } catch (const std::exception& e) {
            std::cerr << "[ SKIP     ] StagingArenaTest: " << e.what() << std::endl;
        }
    }

    static void TearDownTestSuite() {
        ctx.reset();
        window.reset();
        glfwTerminate();
    }

    void SetUp() override {
        if (!m_initialized) {
            GTEST_SKIP() << "VulkanContext not initialized (no compatible GPU)";
        }
        imagePool = std::make_unique<gpu::TransientImagePool>(
            ctx->getDevice(), ctx->getVmaAllocator(), ctx->getBindlessHeap());
        bufferPool = std::make_unique<gpu::TransientBufferPool>(
            ctx->getDevice(), ctx->getVmaAllocator(), ctx->getBindlessHeap());
        core::DEBUG_clearLayoutRegistry();
    }

    void TearDown() override {
        if (m_initialized) {
            imagePool.reset();
            bufferPool.reset();
        }
        core::DEBUG_clearLayoutRegistry();
    }

    static std::unique_ptr<platform::Window> window;
    static std::unique_ptr<gpu::VulkanContext> ctx;
    static bool m_initialized;
    std::unique_ptr<gpu::TransientImagePool> imagePool;
    std::unique_ptr<gpu::TransientBufferPool> bufferPool;
};

std::unique_ptr<platform::Window> StagingArenaTest::window = nullptr;
std::unique_ptr<gpu::VulkanContext> StagingArenaTest::ctx = nullptr;
bool StagingArenaTest::m_initialized = false;

TEST_F(StagingArenaTest, AllocateAdvancesHead) {
    gpu::StagingArena arena(ctx->getVmaAllocator(), 4096);
    EXPECT_EQ(arena.used(), 0u);

    auto a = arena.allocate(256, 16);
    ASSERT_TRUE(a.isValid());
    EXPECT_EQ(a.offset, 0u);
    EXPECT_EQ(arena.used(), 256u);

    auto b = arena.allocate(128, 16);
    ASSERT_TRUE(b.isValid());
    EXPECT_EQ(b.offset, 256u);
    EXPECT_EQ(arena.used(), 384u);
}

TEST_F(StagingArenaTest, AllocateRespectsAlignment) {
    gpu::StagingArena arena(ctx->getVmaAllocator(), 4096);
    auto a = arena.allocate(7, 16);
    ASSERT_TRUE(a.isValid());
    EXPECT_EQ(a.offset, 0u);

    auto b = arena.allocate(8, 256);
    ASSERT_TRUE(b.isValid());
    EXPECT_EQ(b.offset, 256u);  // 7 rounded up to 256
}

TEST_F(StagingArenaTest, AllocateReturnsInvalidOnOOM) {
    gpu::StagingArena arena(ctx->getVmaAllocator(), 1024);
    auto big = arena.allocate(2048, 16);
    EXPECT_FALSE(big.isValid());

    auto small = arena.allocate(512, 16);
    EXPECT_TRUE(small.isValid());
}

TEST_F(StagingArenaTest, ResetReclaimsArena) {
    gpu::StagingArena arena(ctx->getVmaAllocator(), 4096);
    (void)arena.allocate(2048, 16);
    EXPECT_EQ(arena.used(), 2048u);

    arena.reset();
    EXPECT_EQ(arena.used(), 0u);

    auto a = arena.allocate(2048, 16);
    EXPECT_TRUE(a.isValid());
    EXPECT_EQ(a.offset, 0u);
}

TEST_F(StagingArenaTest, MappedPointerIsWritable) {
    // Sanity: the mapped pointer must be writable host memory. Bytes
    // written here are uploaded by a copy command later; this test does
    // the write but not the copy.
    gpu::StagingArena arena(ctx->getVmaAllocator(), 4096);
    auto a = arena.allocate(64, 16);
    ASSERT_TRUE(a.isValid());

    std::vector<uint8_t> pattern(64);
    for (size_t i = 0; i < pattern.size(); ++i) pattern[i] = static_cast<uint8_t>(i * 7);
    std::memcpy(a.mapped, pattern.data(), pattern.size());

    // Read back through the same pointer — host-coherent / mapped memory
    // round-trips by definition.
    EXPECT_EQ(0, std::memcmp(a.mapped, pattern.data(), pattern.size()));
}

// Round-trip a known deep image through staging, then read back the samples
// buffer via vkCmdCopyBuffer and verify identity.
TEST_F(StagingArenaTest, UploadDeepImageRoundTripsSamples) {
    // Build a tiny 4x4 deep image with the 6-channel v1 layout. Sample
    // counts are 0..2 distributed over the grid.
    constexpr uint32_t W = 4;
    constexpr uint32_t H = 4;

    std::vector<core::DeepChannel> channels = {
        {"Z", core::ChannelType::Float32, 1}, {"ZBack", core::ChannelType::Float32, 1},
        {"R", core::ChannelType::Float16, 1}, {"G", core::ChannelType::Float16, 1},
        {"B", core::ChannelType::Float16, 1}, {"A", core::ChannelType::Float16, 1},
    };
    const core::DeepLayout* layout = core::getDeepLayout(channels);

    io::ParsedDeepImage parsed;
    parsed.width = W;
    parsed.height = H;
    parsed.sampleCounts.resize(W * H);
    for (uint32_t i = 0; i < W * H; ++i) parsed.sampleCounts[i] = (i % 3);

    const uint64_t totalSamples = parsed.totalSamples();
    ASSERT_GT(totalSamples, 0u);

    // Channel data: a unique byte pattern per channel × sample so we can
    // detect any interleaving / offset mistake in the upload.
    parsed.channelData.resize(channels.size());
    for (size_t ch = 0; ch < channels.size(); ++ch) {
        uint32_t bytes = core::channelTypeBytes(channels[ch].type) * channels[ch].components;
        parsed.channelData[ch].resize(bytes * totalSamples);
        for (uint64_t s = 0; s < totalSamples; ++s) {
            for (uint32_t b = 0; b < bytes; ++b) {
                parsed.channelData[ch][s * bytes + b] =
                    static_cast<uint8_t>((ch * 31 + s * 7 + b) & 0xFF);
            }
        }
    }

    // Issue the upload.
    gpu::StagingArena arena(ctx->getVmaAllocator(), 64 * 1024);

    VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
    gpu::ResourceRef ref =
        gpu::uploadDeepImage(cmd, arena, *imagePool, *bufferPool, parsed, *layout);
    ASSERT_EQ(ref.kind, gpu::ResourceRef::Kind::Deep);
    ASSERT_TRUE(ref.deep.samples.isValid());
    ASSERT_EQ(ref.deep.layout, layout);

    // Read back the samples buffer to host memory and verify the AoS
    // interleave matches the expected packed form.
    const VkDeviceSize sampleBytes = totalSamples * layout->stride();
    VkBufferCreateInfo readbackInfo{};
    readbackInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    readbackInfo.size = sampleBytes;
    readbackInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo readbackAlloc{};
    readbackAlloc.usage = VMA_MEMORY_USAGE_AUTO;
    readbackAlloc.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer readback = VK_NULL_HANDLE;
    VmaAllocation readbackVma = VK_NULL_HANDLE;
    VmaAllocationInfo readbackInfoOut{};
    ASSERT_EQ(VK_SUCCESS, vmaCreateBuffer(ctx->getVmaAllocator(), &readbackInfo, &readbackAlloc,
                                          &readback, &readbackVma, &readbackInfoOut));

    VkBufferCopy copyAll{};
    copyAll.size = sampleBytes;
    vkCmdCopyBuffer(cmd, bufferPool->getBuffer(ref.deep.samples), readback, 1, &copyAll);
    ctx->endSingleTimeCommands(cmd);

    std::vector<uint8_t> packedActual(sampleBytes);
    std::memcpy(packedActual.data(), readbackInfoOut.pMappedData, sampleBytes);

    // Build the expected packed AoS layout the same way uploadDeepImage
    // does and compare byte-for-byte.
    std::vector<uint8_t> packedExpected(sampleBytes, 0);
    for (size_t ch = 0; ch < channels.size(); ++ch) {
        uint32_t off = layout->byteOffset(static_cast<int32_t>(ch));
        uint32_t size = layout->byteSize(static_cast<int32_t>(ch));
        for (uint64_t s = 0; s < totalSamples; ++s) {
            std::memcpy(packedExpected.data() + s * layout->stride() + off,
                        parsed.channelData[ch].data() + s * size, size);
        }
    }

    EXPECT_EQ(packedActual, packedExpected);

    vmaDestroyBuffer(ctx->getVmaAllocator(), readback, readbackVma);
    imagePool->release(ref.deep.countImage);
    imagePool->release(ref.deep.offsetImage);
    bufferPool->release(ref.deep.samples);
    imagePool->flushPendingReleases();
    bufferPool->flushPendingReleases();
}
