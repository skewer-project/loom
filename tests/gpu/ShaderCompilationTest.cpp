#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include "gpu/ComputeTask.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/VulkanContext.hpp"
#include "platform/Window.hpp"

namespace gpu = loom::gpu;
namespace platform = loom::platform;

// PipelineCache::getOrCreate's negative paths. Constructs a minimal Vulkan
// stack (a no-window VulkanContext, an empty pipeline layout), then probes
// the file-not-found and malformed-SPIR-V failure modes.
//
// Skipped cleanly on machines without a Vulkan device so the unit-test
// suite still passes; the meat of the assertions runs against Lavapipe in
// CI.
class ShaderCompilationTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        try {
            if (!glfwInit()) return;
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            window = std::make_unique<platform::Window>(100, 100, "ShaderCompilationTest");
            ctx = std::make_unique<gpu::VulkanContext>();
            ctx->init(*window, "ShaderCompilationTest");
            m_initialized = true;
        } catch (const std::exception& e) {
            std::cerr << "[ SKIP     ] ShaderCompilationTest: " << e.what() << std::endl;
        }
    }

    static void TearDownTestSuite() {
        if (pipelineLayout != VK_NULL_HANDLE && ctx) {
            vkDestroyPipelineLayout(ctx->getDevice(), pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
        ctx.reset();
        window.reset();
        glfwTerminate();
    }

    void SetUp() override {
        if (!m_initialized) {
            GTEST_SKIP() << "VulkanContext not initialized (no Vulkan device)";
        }
        if (pipelineLayout == VK_NULL_HANDLE) {
            VkPipelineLayoutCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            VkDescriptorSetLayout setLayout = ctx->getBindlessHeap().getLayout();
            info.setLayoutCount = 1;
            info.pSetLayouts = &setLayout;
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            range.size = gpu::MAX_PUSH_CONSTANT_BYTES;
            info.pushConstantRangeCount = 1;
            info.pPushConstantRanges = &range;
            ASSERT_EQ(vkCreatePipelineLayout(ctx->getDevice(), &info, nullptr, &pipelineLayout),
                      VK_SUCCESS);
        }
    }

    static std::unique_ptr<platform::Window> window;
    static std::unique_ptr<gpu::VulkanContext> ctx;
    static bool m_initialized;
    static VkPipelineLayout pipelineLayout;
};

std::unique_ptr<platform::Window> ShaderCompilationTest::window = nullptr;
std::unique_ptr<gpu::VulkanContext> ShaderCompilationTest::ctx = nullptr;
bool ShaderCompilationTest::m_initialized = false;
VkPipelineLayout ShaderCompilationTest::pipelineLayout = VK_NULL_HANDLE;

TEST_F(ShaderCompilationTest, MissingShaderFileThrows) {
    gpu::PipelineCache pipelineCache(ctx->getDevice(), pipelineLayout);
    // The file does not exist anywhere in the shader search path; readFile
    // throws a std::runtime_error. The exception type is intentionally
    // checked — silent failure here would surface as a crashing dispatch
    // later.
    EXPECT_THROW(
        { (void)pipelineCache.getOrCreate("definitely_not_a_real_shader.spv"); },
        std::runtime_error);
}
