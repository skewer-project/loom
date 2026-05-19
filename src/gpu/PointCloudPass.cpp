#include "gpu/PointCloudPass.hpp"

#include <cstring>
#include <stdexcept>

#include "core/Camera.hpp"
#include "core/Log.hpp"
#include "gpu/PipelineCache.hpp"

namespace loom::gpu {

namespace {

struct PointCloudPC {
    uint32_t samplesSlot;
    uint32_t sampleToPixelSlot;
    uint32_t width;
    uint32_t height;
    float zScale;
    float pointSize;
};

}  // namespace

PointCloudPass::PointCloudPass(VkDevice device, VmaAllocator allocator,
                               VkDescriptorSetLayout bindlessLayout, PipelineCache& pipelineCache,
                               VkFormat colorFormat, VkFormat depthFormat)
    : m_device(device),
      m_allocator(allocator),
      m_bindlessLayout(bindlessLayout),
      m_pipelineCache(pipelineCache),
      m_colorFormat(colorFormat),
      m_depthFormat(depthFormat) {
    createDescriptorResources();
    createCameraBuffer();
    createPipelineLayout();
}

PointCloudPass::~PointCloudPass() {
    destroyDepth();
    if (m_cameraBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_cameraBuffer, m_cameraAlloc);
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    }
    if (m_cameraSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_cameraSetLayout, nullptr);
    }
}

void PointCloudPass::createDescriptorResources() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_cameraSetLayout) !=
        VK_SUCCESS) {
        throw std::runtime_error("PointCloudPass: failed to create camera set layout");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("PointCloudPass: failed to create descriptor pool");
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = m_descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_cameraSetLayout;
    if (vkAllocateDescriptorSets(m_device, &alloc, &m_cameraSet) != VK_SUCCESS) {
        throw std::runtime_error("PointCloudPass: failed to allocate camera descriptor set");
    }
}

void PointCloudPass::createCameraBuffer() {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = sizeof(glm::mat4);
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    alloc.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo result{};
    if (vmaCreateBuffer(m_allocator, &info, &alloc, &m_cameraBuffer, &m_cameraAlloc, &result) !=
        VK_SUCCESS) {
        throw std::runtime_error("PointCloudPass: vmaCreateBuffer (camera UBO) failed");
    }
    m_cameraMapped = result.pMappedData;

    VkDescriptorBufferInfo bi{};
    bi.buffer = m_cameraBuffer;
    bi.offset = 0;
    bi.range = sizeof(glm::mat4);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_cameraSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bi;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void PointCloudPass::createPipelineLayout() {
    VkDescriptorSetLayout sets[2] = {m_bindlessLayout, m_cameraSetLayout};

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc.offset = 0;
    pc.size = sizeof(PointCloudPC);

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 2;
    info.pSetLayouts = sets;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_device, &info, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("PointCloudPass: vkCreatePipelineLayout failed");
    }
}

void PointCloudPass::destroyDepth() {
    if (m_depthView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_depthView, nullptr);
        m_depthView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, m_depthImage, m_depthAlloc);
        m_depthImage = VK_NULL_HANDLE;
        m_depthAlloc = VK_NULL_HANDLE;
    }
    m_depthW = 0;
    m_depthH = 0;
}

void PointCloudPass::ensureDepth(uint32_t width, uint32_t height) {
    if (m_depthImage != VK_NULL_HANDLE && m_depthW == width && m_depthH == height) return;
    destroyDepth();

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = m_depthFormat;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(m_allocator, &info, &alloc, &m_depthImage, &m_depthAlloc, nullptr) !=
        VK_SUCCESS) {
        throw std::runtime_error("PointCloudPass: vmaCreateImage (depth) failed");
    }

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = m_depthImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = m_depthFormat;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(m_device, &view, nullptr, &m_depthView) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, m_depthImage, m_depthAlloc);
        m_depthImage = VK_NULL_HANDLE;
        m_depthAlloc = VK_NULL_HANDLE;
        throw std::runtime_error("PointCloudPass: vkCreateImageView (depth) failed");
    }
    m_depthW = width;
    m_depthH = height;
}

void PointCloudPass::record(VkCommandBuffer cmd, const ResourceRef::DeepRef& deep, VkImage dstImage,
                            VkImageView dstImageView, VkDescriptorSet bindlessSet, uint32_t width,
                            uint32_t height, const core::Camera& camera, float zScale,
                            float pointSize) {
    if (!deep.samples.isValid() || !deep.sampleToPixel.isValid() || deep.totalSamples == 0) {
        // Nothing to draw — caller should still have transitioned dstImage
        // somewhere sensible. We fall through without recording any work.
        return;
    }

    ensureDepth(width, height);

    // Update camera UBO with viewProj.
    const glm::mat4 viewProj = camera.viewProj();
    std::memcpy(m_cameraMapped, &viewProj, sizeof(viewProj));

    // Resolve pipeline via the cache.
    GraphicsPipelineKey key;
    key.vertSpv = "PointCloud.vert.spv";
    key.fragSpv = "PointCloud.frag.spv";
    key.layout = m_pipelineLayout;
    key.vertexInput = VertexInputDesc::None;
    key.topology = Topology::PointList;
    key.blend = BlendMode::Opaque;
    key.depth = DepthMode::TestWrite;
    key.colorFormat = m_colorFormat;
    key.depthFormat = m_depthFormat;
    key.samples = VK_SAMPLE_COUNT_1_BIT;
    VkPipeline pipeline = m_pipelineCache.getOrCreateGraphics(key);

    // Barrier 1: dst image UNDEFINED → COLOR_ATTACHMENT_OPTIMAL,
    //            depth UNDEFINED → DEPTH_ATTACHMENT_OPTIMAL.
    VkImageMemoryBarrier2 barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barriers[0].srcAccessMask = VK_ACCESS_2_NONE;
    barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].image = dstImage;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barriers[1].srcAccessMask = VK_ACCESS_2_NONE;
    barriers[1].dstStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barriers[1].image = m_depthImage;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    VkDependencyInfo pre{};
    pre.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    pre.imageMemoryBarrierCount = 2;
    pre.pImageMemoryBarriers = barriers;
    vkCmdPipelineBarrier2(cmd, &pre);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = dstImageView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.05f, 0.05f, 0.07f, 1.0f}};

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = m_depthView;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea = {{0, 0}, {width, height}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;

    vkCmdBeginRendering(cmd, &ri);

    VkViewport vp{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {width, height}};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    VkDescriptorSet sets[2] = {bindlessSet, m_cameraSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 2, sets, 0,
                            nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    PointCloudPC pcData{};
    pcData.samplesSlot = deep.samples.bindlessSlot;
    pcData.sampleToPixelSlot = deep.sampleToPixel.bindlessSlot;
    pcData.width = deep.width;
    pcData.height = deep.height;
    pcData.zScale = zScale;
    pcData.pointSize = pointSize;
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pcData),
                       &pcData);

    vkCmdDraw(cmd, static_cast<uint32_t>(deep.totalSamples), 1, 0, 0);

    vkCmdEndRendering(cmd);

    // Transition dst to SHADER_READ_ONLY for ImGui sampling.
    VkImageMemoryBarrier2 post{};
    post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    post.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    post.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    post.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    post.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    post.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post.image = dstImage;
    post.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo postDep{};
    postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    postDep.imageMemoryBarrierCount = 1;
    postDep.pImageMemoryBarriers = &post;
    vkCmdPipelineBarrier2(cmd, &postDep);
}

}  // namespace loom::gpu
