#include <gtest/gtest.h>

#include "core/ColorManagement.hpp"
#include "gpu/BindlessHeap.hpp"
#include "gpu/DisplayPass.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"

namespace gpu = loom::gpu;
namespace platform = loom::platform;

// Tests render into an R8G8B8A8_UNORM destination; the production codepath
// would apply DisplayTransform::sRGB for such a target (the swapchain hardware
// does not encode for UNORM). The existing tolerances (EXPECT_NEAR with ±5)
// accommodate the slight difference between sRGB OETF and the legacy
// pow(v, 1/2.2) approximation that the test math was originally written for.
static constexpr uint32_t kDisplayTransformSRGB =
    static_cast<uint32_t>(loom::color::DisplayTransform::sRGB);
static constexpr float kExposure = 1.0f;

class DisplayPassTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        try {
            if (!glfwInit()) {
                return;
            }
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            window = std::make_unique<platform::Window>(100, 100, "DisplayPassTest");
            ctx = std::make_unique<gpu::VulkanContext>();
            ctx->init(*window, "DisplayPassTest");
            m_initialized = true;
        } catch (const std::exception& e) {
            std::cerr << "[ SKIP     ] DisplayPassTest: " << e.what() << std::endl;
        }
    }

    static void TearDownTestSuite() {
        ctx.reset();
        window.reset();
        glfwTerminate();
    }

    void SetUp() override {
        if (!m_initialized) {
            GTEST_SKIP() << "VulkanContext not initialized";
        }
#ifndef LOOM_HAS_SHADER_COMPILER
        GTEST_SKIP() << "Shader compiler not found, skipping DisplayPass tests";
#endif
        displayPass = std::make_unique<gpu::DisplayPass>(ctx->getDevice(), VK_FORMAT_R8G8B8A8_UNORM,
                                                         ctx->getBindlessHeap().getLayout());

        imagePool = std::make_unique<gpu::TransientImagePool>(
            ctx->getDevice(), ctx->getVmaAllocator(), ctx->getBindlessHeap());
    }

    void TearDown() override {
        if (m_initialized) {
            imagePool.reset();
            displayPass.reset();
        }
    }

    static std::unique_ptr<platform::Window> window;
    static std::unique_ptr<gpu::VulkanContext> ctx;
    static bool m_initialized;

    std::unique_ptr<gpu::DisplayPass> displayPass;
    std::unique_ptr<gpu::TransientImagePool> imagePool;

    // Helper to read back pixels from an image
    std::vector<uint8_t> readBackImage(VkImage image, uint32_t width, uint32_t height) {
        VkDeviceSize size = width * height * 4;
        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

        vmaCreateBuffer(ctx->getVmaAllocator(), &bufferInfo, &allocInfo, &stagingBuffer,
                        &stagingAlloc, nullptr);

        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};

        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1,
                               &copy);

        ctx->endSingleTimeCommands(cmd);

        void* data;
        vmaMapMemory(ctx->getVmaAllocator(), stagingAlloc, &data);
        std::vector<uint8_t> pixels(size);
        memcpy(pixels.data(), data, size);
        vmaUnmapMemory(ctx->getVmaAllocator(), stagingAlloc);

        vmaDestroyBuffer(ctx->getVmaAllocator(), stagingBuffer, stagingAlloc);

        return pixels;
    }

    void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                               VkImageLayout newLayout) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }
};

std::unique_ptr<platform::Window> DisplayPassTest::window = nullptr;
std::unique_ptr<gpu::VulkanContext> DisplayPassTest::ctx = nullptr;
bool DisplayPassTest::m_initialized = false;

TEST_F(DisplayPassTest, LinearPassthrough) {
    uint32_t width = 64;
    uint32_t height = 64;

    // 1. Create source HDR image (red)
    gpu::ImageSpec spec{VK_FORMAT_R32G32B32A32_SFLOAT,
                        {width, height},
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    gpu::ImageHandle hdrHandle = imagePool->acquire(spec);
    VkImage hdrImage = imagePool->getImage(hdrHandle);
    uint32_t hdrSlot = hdrHandle.bindlessSlot;

    // Fill HDR image with [1.0, 0.0, 0.0, 1.0]
    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = hdrImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        VkClearColorValue clearColor = {{1.0f, 0.0f, 0.0f, 1.0f}};
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, hdrImage, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);

        ctx->endSingleTimeCommands(cmd);
    }

    // 2. Create destination LDR image
    VkImage dstImage;
    VkImageView dstImageView;
    VmaAllocation dstAlloc;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vmaCreateImage(ctx->getVmaAllocator(), &imageInfo, &allocInfo, &dstImage, &dstAlloc, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = dstImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx->getDevice(), &viewInfo, nullptr, &dstImageView);

    // 3. Record and submit display pass
    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        displayPass->record(cmd, hdrImage, dstImage, dstImageView,
                            ctx->getBindlessHeap().getDescriptorSet(), hdrSlot, width, height,
                            width, height,
                            /*toneMapMode=*/0, kDisplayTransformSRGB, kExposure);
        ctx->endSingleTimeCommands(cmd);
    }

    // 4. Read back and verify
    std::vector<uint8_t> pixels = readBackImage(dstImage, width, height);

    // Linear 1.0 -> sRGB OETF (saturates at 1.0) -> 1.0 -> 255
    EXPECT_GE(pixels[0], 250);  // R
    EXPECT_LE(pixels[1], 5);    // G
    EXPECT_LE(pixels[2], 5);    // B
    EXPECT_GE(pixels[3], 250);  // A

    // Cleanup
    vkDestroyImageView(ctx->getDevice(), dstImageView, nullptr);
    vmaDestroyImage(ctx->getVmaAllocator(), dstImage, dstAlloc);
    imagePool->release(hdrHandle);
}

TEST_F(DisplayPassTest, ToneMapModes) {
    uint32_t width = 4;
    uint32_t height = 4;

    // Overexposed HDR [4.0, 2.0, 1.0, 1.0]
    gpu::ImageSpec spec{VK_FORMAT_R32G32B32A32_SFLOAT,
                        {width, height},
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    gpu::ImageHandle hdrHandle = imagePool->acquire(spec);
    VkImage hdrImage = imagePool->getImage(hdrHandle);
    uint32_t hdrSlot = hdrHandle.bindlessSlot;

    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        transitionImageLayout(cmd, hdrImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        VkClearColorValue clearColor = {{4.0f, 2.0f, 1.0f, 1.0f}};
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, hdrImage, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
        ctx->endSingleTimeCommands(cmd);
    }

    VkImage dstImage;
    VkImageView dstImageView;
    VmaAllocation dstAlloc;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vmaCreateImage(ctx->getVmaAllocator(), &imageInfo, &allocInfo, &dstImage, &dstAlloc, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = dstImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx->getDevice(), &viewInfo, nullptr, &dstImageView);

    // Test Reinhard
    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        displayPass->record(cmd, hdrImage, dstImage, dstImageView,
                            ctx->getBindlessHeap().getDescriptorSet(), hdrSlot, width, height,
                            width, height,
                            /*toneMapMode=*/1, kDisplayTransformSRGB, kExposure);
        ctx->endSingleTimeCommands(cmd);

        std::vector<uint8_t> pixels = readBackImage(dstImage, width, height);
        // Reinhard: 4.0 / (4.0 + 1.0) = 0.8.
        // sRGB OETF of 0.8 ≈ 0.906 (close to the legacy pow(0.8, 1/2.2) ≈ 0.903).
        // 0.906 * 255 ≈ 231; tolerance ±5 covers both encodings.
        EXPECT_NEAR(pixels[0], 230, 5);
    }

    // Test ACES
    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        displayPass->record(cmd, hdrImage, dstImage, dstImageView,
                            ctx->getBindlessHeap().getDescriptorSet(), hdrSlot, width, height,
                            width, height,
                            /*toneMapMode=*/2, kDisplayTransformSRGB, kExposure);
        ctx->endSingleTimeCommands(cmd);

        std::vector<uint8_t> pixels = readBackImage(dstImage, width, height);
        // ACES will be different but should be less than 255 and more than 0.
        EXPECT_GT(pixels[0], 0);
        EXPECT_LT(pixels[0], 255);
    }

    vkDestroyImageView(ctx->getDevice(), dstImageView, nullptr);
    vmaDestroyImage(ctx->getVmaAllocator(), dstImage, dstAlloc);
    imagePool->release(hdrHandle);
}

// Aspect-fit letterboxing — the fix for the "garbage outside rendered
// region" glitch surfaced in Phase B.8 follow-up. When the source HDR
// image's aspect ratio differs from the destination viewport's, the
// fragment shader fits the source inside the viewport at native aspect
// and fills the leftover strip with opaque black.
TEST_F(DisplayPassTest, LetterboxesWiderSource) {
    // Source 64×16 (4:1), destination 64×64 (1:1) — source is "wider," so
    // letterbox top + bottom. Center row sees source; top/bottom rows
    // see black.
    constexpr uint32_t srcW = 64, srcH = 16;
    constexpr uint32_t dstW = 64, dstH = 64;

    gpu::ImageSpec spec{VK_FORMAT_R32G32B32A32_SFLOAT,
                        {srcW, srcH},
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    gpu::ImageHandle hdrHandle = imagePool->acquire(spec);
    VkImage hdrImage = imagePool->getImage(hdrHandle);
    uint32_t hdrSlot = hdrHandle.bindlessSlot;

    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        transitionImageLayout(cmd, hdrImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        VkClearColorValue clearColor = {{1.0f, 0.0f, 0.0f, 1.0f}};  // red
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, hdrImage, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
        ctx->endSingleTimeCommands(cmd);
    }

    VkImage dstImage;
    VkImageView dstImageView;
    VmaAllocation dstAlloc;
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {dstW, dstH, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaCreateImage(ctx->getVmaAllocator(), &imageInfo, &allocInfo, &dstImage, &dstAlloc, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = dstImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx->getDevice(), &viewInfo, nullptr, &dstImageView);

    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        displayPass->record(cmd, hdrImage, dstImage, dstImageView,
                            ctx->getBindlessHeap().getDescriptorSet(), hdrSlot, dstW, dstH, srcW,
                            srcH, /*toneMapMode=*/0, kDisplayTransformSRGB, kExposure);
        ctx->endSingleTimeCommands(cmd);
    }

    std::vector<uint8_t> pixels = readBackImage(dstImage, dstW, dstH);
    auto px = [&](uint32_t x, uint32_t y) -> const uint8_t* { return &pixels[(y * dstW + x) * 4]; };

    // Row 32 (vertical center): fitted source → red.
    EXPECT_GE(px(32, 32)[0], 230);
    EXPECT_LE(px(32, 32)[1], 10);
    EXPECT_LE(px(32, 32)[2], 10);

    // Row 0 (top edge): outside fitted rect → black.
    EXPECT_LE(px(32, 0)[0], 5);
    EXPECT_LE(px(32, 0)[1], 5);
    EXPECT_LE(px(32, 0)[2], 5);
    EXPECT_GE(px(32, 0)[3], 250);

    // Row 63 (bottom edge): outside fitted rect → black.
    EXPECT_LE(px(32, 63)[0], 5);

    vkDestroyImageView(ctx->getDevice(), dstImageView, nullptr);
    vmaDestroyImage(ctx->getVmaAllocator(), dstImage, dstAlloc);
    imagePool->release(hdrHandle);
}

TEST_F(DisplayPassTest, LetterboxesTallerSource) {
    // Source 16×64 (1:4), destination 64×64 (1:1) — source is "taller,"
    // so letterbox left + right.
    constexpr uint32_t srcW = 16, srcH = 64;
    constexpr uint32_t dstW = 64, dstH = 64;

    gpu::ImageSpec spec{VK_FORMAT_R32G32B32A32_SFLOAT,
                        {srcW, srcH},
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    gpu::ImageHandle hdrHandle = imagePool->acquire(spec);
    VkImage hdrImage = imagePool->getImage(hdrHandle);
    uint32_t hdrSlot = hdrHandle.bindlessSlot;

    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        transitionImageLayout(cmd, hdrImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        VkClearColorValue clearColor = {{0.0f, 1.0f, 0.0f, 1.0f}};  // green
        VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, hdrImage, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
        ctx->endSingleTimeCommands(cmd);
    }

    VkImage dstImage;
    VkImageView dstImageView;
    VmaAllocation dstAlloc;
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {dstW, dstH, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaCreateImage(ctx->getVmaAllocator(), &imageInfo, &allocInfo, &dstImage, &dstAlloc, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = dstImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx->getDevice(), &viewInfo, nullptr, &dstImageView);

    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        displayPass->record(cmd, hdrImage, dstImage, dstImageView,
                            ctx->getBindlessHeap().getDescriptorSet(), hdrSlot, dstW, dstH, srcW,
                            srcH, /*toneMapMode=*/0, kDisplayTransformSRGB, kExposure);
        ctx->endSingleTimeCommands(cmd);
    }

    std::vector<uint8_t> pixels = readBackImage(dstImage, dstW, dstH);
    auto px = [&](uint32_t x, uint32_t y) -> const uint8_t* { return &pixels[(y * dstW + x) * 4]; };

    // Column 32 (horizontal center): fitted source → green.
    EXPECT_LE(px(32, 32)[0], 10);
    EXPECT_GE(px(32, 32)[1], 230);
    EXPECT_LE(px(32, 32)[2], 10);

    // Column 0 (left edge): outside fitted rect → black.
    EXPECT_LE(px(0, 32)[0], 5);
    EXPECT_LE(px(0, 32)[1], 5);
    EXPECT_LE(px(0, 32)[2], 5);

    // Column 63 (right edge): outside fitted rect → black.
    EXPECT_LE(px(63, 32)[1], 5);

    vkDestroyImageView(ctx->getDevice(), dstImageView, nullptr);
    vmaDestroyImage(ctx->getVmaAllocator(), dstImage, dstAlloc);
    imagePool->release(hdrHandle);
}
