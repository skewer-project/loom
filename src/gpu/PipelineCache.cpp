#include "gpu/PipelineCache.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace loom::gpu {

static std::vector<char> readFile(const std::string& filename) {
    std::filesystem::path path(filename);

#ifdef LOOM_SHADER_DIR
    // If filename is relative, prepend the shader directory
    if (path.is_relative()) {
        path = std::filesystem::path(LOOM_SHADER_DIR) / path;
    }
#endif

    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + path.string());
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
}

PipelineCache::PipelineCache(VkDevice device, VkPipelineLayout pipelineLayout)
    : m_device(device), m_pipelineLayout(pipelineLayout) {}

PipelineCache::~PipelineCache() {
    for (auto& [path, pipeline] : m_pipelines) {
        vkDestroyPipeline(m_device, pipeline, nullptr);
    }
}

VkPipeline PipelineCache::getOrCreate(const std::string& spvPath) {
    if (m_pipelines.contains(spvPath)) {
        return m_pipelines[spvPath];
    }

    auto shaderCode = readFile(spvPath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module for: " + spvPath);
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";

    VkPipeline pipeline;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) !=
        VK_SUCCESS) {
        vkDestroyShaderModule(m_device, shaderModule, nullptr);
        throw std::runtime_error("failed to create compute pipeline for: " + spvPath);
    }

    vkDestroyShaderModule(m_device, shaderModule, nullptr);

    m_pipelines[spvPath] = pipeline;
    return pipeline;
}

}  // namespace loom::gpu
