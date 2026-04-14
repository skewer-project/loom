#include <gtest/gtest.h>

#include "gpu/TransientBufferPool.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"

namespace gpu = loom::gpu;
namespace platform = loom::platform;

class ResourcePoolTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        try {
            if (!glfwInit()) {
                return;
            }
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            window = std::make_unique<platform::Window>(100, 100, "Test");
            ctx = std::make_unique<gpu::VulkanContext>();
            ctx->init(*window, "ResourcePoolTest");
            m_initialized = true;
        } catch (const std::exception& e) {
            std::cerr << "[ SKIP     ] ResourcePoolTest: " << e.what() << std::endl;
        }
    }

    static void TearDownTestSuite() {
        ctx.reset();
        window.reset();
        glfwTerminate();
    }

    void SetUp() override {
        if (!m_initialized) {
            GTEST_SKIP() << "VulkanContext not initialized (No compatible GPU found)";
        }
        imagePool = std::make_unique<gpu::TransientImagePool>(
            ctx->getDevice(), ctx->getVmaAllocator(), ctx->getBindlessHeap());
        bufferPool = std::make_unique<gpu::TransientBufferPool>(
            ctx->getDevice(), ctx->getVmaAllocator(), ctx->getBindlessHeap());
    }

    void TearDown() override {
        if (m_initialized) {
            imagePool.reset();
            bufferPool.reset();
        }
    }

    static std::unique_ptr<platform::Window> window;
    static std::unique_ptr<gpu::VulkanContext> ctx;
    static bool m_initialized;
    std::unique_ptr<gpu::TransientImagePool> imagePool;
    std::unique_ptr<gpu::TransientBufferPool> bufferPool;
};

std::unique_ptr<platform::Window> ResourcePoolTest::window = nullptr;
std::unique_ptr<gpu::VulkanContext> ResourcePoolTest::ctx = nullptr;
bool ResourcePoolTest::m_initialized = false;

TEST_F(ResourcePoolTest, ImageRecycle) {
    gpu::ImageSpec spec{VK_FORMAT_R8G8B8A8_UNORM, {1920, 1080}, VK_IMAGE_USAGE_SAMPLED_BIT};
    gpu::ImageHandle h1 = imagePool->acquire(spec);
    uint32_t slot1 = imagePool->DEBUG_getBindlessSlot(h1);

    imagePool->release(h1);
    imagePool->flushPendingReleases();

    gpu::ImageHandle h2 = imagePool->acquire(spec);
    uint32_t slot2 = imagePool->DEBUG_getBindlessSlot(h2);

    EXPECT_EQ(slot1, slot2);
}

TEST_F(ResourcePoolTest, ImageSpecMismatch) {
    gpu::ImageSpec spec1{VK_FORMAT_R8G8B8A8_UNORM, {1920, 1080}, VK_IMAGE_USAGE_SAMPLED_BIT};
    gpu::ImageSpec spec2{VK_FORMAT_R8G8B8A8_UNORM, {1024, 1024}, VK_IMAGE_USAGE_SAMPLED_BIT};

    gpu::ImageHandle h1 = imagePool->acquire(spec1);
    gpu::ImageHandle h2 = imagePool->acquire(spec2);

    EXPECT_NE(imagePool->DEBUG_getBindlessSlot(h1), imagePool->DEBUG_getBindlessSlot(h2));
}

TEST_F(ResourcePoolTest, StaleHandleRejection) {
    gpu::ImageSpec spec{VK_FORMAT_R8G8B8A8_UNORM, {100, 100}, VK_IMAGE_USAGE_SAMPLED_BIT};
    gpu::ImageHandle h1 = imagePool->acquire(spec);

    imagePool->release(h1);
    imagePool->flushPendingReleases();

    // Attempting to release h1 again should throw due to generation mismatch
    EXPECT_THROW(imagePool->release(h1), std::runtime_error);
}

TEST_F(ResourcePoolTest, FreeListExhaustion) {
    // Acquire 2048 distinct image specs to exhaust the heap
    for (uint32_t i = 0; i < 2048; ++i) {
        gpu::ImageSpec spec{VK_FORMAT_R8G8B8A8_UNORM, {1 + i, 1}, VK_IMAGE_USAGE_SAMPLED_BIT};
        imagePool->acquire(spec);
    }

    // 2049th should return sentinel value
    gpu::ImageSpec spec_last{VK_FORMAT_R8G8B8A8_UNORM, {4000, 4000}, VK_IMAGE_USAGE_SAMPLED_BIT};
    gpu::ImageHandle h_last = imagePool->acquire(spec_last);

    EXPECT_EQ(h_last.poolIndex, 0xFFFFFFFF);
    EXPECT_EQ(h_last.bindlessSlot, 0xFFFFFFFF);
    EXPECT_EQ(h_last.generation, 0xFFFFFFFF);
}

TEST_F(ResourcePoolTest, BufferBounds) {
    gpu::BufferHandle h1024 = bufferPool->acquire(1024);
    bufferPool->release(h1024);
    bufferPool->flushPendingReleases();

    // Acquire 4096 - should be a new slot (exceeded 2x bound)
    gpu::BufferHandle h4096 = bufferPool->acquire(4096);
    EXPECT_NE(bufferPool->DEBUG_getBindlessSlot(h1024), bufferPool->DEBUG_getBindlessSlot(h4096));

    // Acquire 512 - should reuse the 1024 slot
    gpu::BufferHandle h512 = bufferPool->acquire(512);
    EXPECT_EQ(bufferPool->DEBUG_getBindlessSlot(h1024), bufferPool->DEBUG_getBindlessSlot(h512));
}
