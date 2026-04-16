#include <gtest/gtest.h>

#include "gpu/BindlessHeap.hpp"
#include "gpu/DisplayPass.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"

namespace gpu = loom::gpu;
namespace platform = loom::platform;

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

        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

        vmaCreateBuffer(ctx->getVmaAllocator(), &bufferInfo, &allocInfo, &stagingBuffer,
                        &stagingAlloc, nullptr);

        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();

        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
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
};

std::unique_ptr<platform::Window> DisplayPassTest::window = nullptr;
std::unique_ptr<gpu::VulkanContext> DisplayPassTest::ctx = nullptr;
bool DisplayPassTest::m_initialized = false;

TEST_F(DisplayPassTest, LinearPassthrough) {
    uint32_t width = 64;
    uint32_t height = 64;

    // 1. Create source HDR image (red)
    gpu::ImageHandle hdrHandle = imagePool->acquire({width, height}, VK_FORMAT_R32G32B32A32_SFLOAT);
    VkImage hdrImage = imagePool->getVkImage(hdrHandle);
    uint32_t hdrSlot = imagePool->getBindlessSlot(hdrHandle);

    // Fill HDR image with [1.0, 0.0, 0.0, 1.0]
    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();

        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = hdrImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
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

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
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

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
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
                            0);  // 0 = Linear
        ctx->endSingleTimeCommands(cmd);
    }

    // 4. Read back and verify
    std::vector<uint8_t> pixels = readBackImage(dstImage, width, height);

    // Linear 1.0 -> Gamma 2.2 -> (1.0 ^ (1/2.2)) -> 1.0 -> 255
    // Wait, the shader applies pow(color, vec3(1.0/2.2)).
    // 1.0 ^ (1/2.2) is 1.0.
    // Let's test with a value that changes.
    // If I use 0.5 HDR: 0.5 ^ (1/2.2) approx 0.729. 0.729 * 255 approx 186.

    // For 1.0, it should be 255.
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
    gpu::ImageHandle hdrHandle = imagePool->acquire({width, height}, VK_FORMAT_R32G32B32_SFLOAT);
    VkImage hdrImage = imagePool->getVkImage(hdrHandle);
    uint32_t hdrSlot = imagePool->getBindlessSlot(hdrHandle);

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

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
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

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
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
                            1);  // 1 = Reinhard
        ctx->endSingleTimeCommands(cmd);

        std::vector<uint8_t> pixels = readBackImage(dstImage, width, height);
        // Reinhard: 4.0 / (4.0 + 1.0) = 0.8.
        // Gamma: 0.8 ^ (1/2.2) approx 0.903. 0.903 * 255 approx 230.
        EXPECT_NEAR(pixels[0], 230, 5);
    }

    // Test ACES
    {
        VkCommandBuffer cmd = ctx->beginSingleTimeCommands();
        displayPass->record(cmd, hdrImage, dstImage, dstImageView,
                            ctx->getBindlessHeap().getDescriptorSet(), hdrSlot, width, height,
                            2);  // 2 = ACES
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

void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                           VkImageLayout newLayout) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

    VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);
}
