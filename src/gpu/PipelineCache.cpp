#include "gpu/PipelineCache.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "core/Assert.hpp"
#include "platform/UserDataDir.hpp"

namespace loom::gpu {

namespace {

namespace fs = std::filesystem;

fs::path cacheFilePath() {
    try {
        return loom::platform::userDataDir() / "pipeline_cache.bin";
    } catch (const std::exception& e) {
        // If we can't resolve a user-data dir we degrade to an in-memory
        // cache for this run.
        std::cerr << "PipelineCache: could not resolve user-data dir (" << e.what()
                  << "); pipeline cache will not be persisted." << std::endl;
        return {};
    }
}

std::vector<char> readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    const size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}

std::vector<char> loadSpirv(const std::string& spvPath) {
    fs::path path(spvPath);
#ifdef LOOM_SHADER_DIR
    if (path.is_relative()) {
        path = fs::path(LOOM_SHADER_DIR) / path;
    }
#endif
    return readFile(path);
}

std::vector<uint8_t> tryReadCacheBlob(const fs::path& path) {
    if (path.empty()) return {};
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return {};
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};
    const auto sz = static_cast<std::streamsize>(file.tellg());
    if (sz <= 0) return {};
    std::vector<uint8_t> blob(static_cast<size_t>(sz));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(blob.data()), sz);
    if (!file) return {};
    return blob;
}

}  // namespace

PipelineCache::PipelineCache(VkDevice device, VkPipelineLayout pipelineLayout)
    : m_device(device), m_pipelineLayout(pipelineLayout) {
    createVkPipelineCache();
}

PipelineCache::~PipelineCache() {
    for (auto& [path, pipeline] : m_pipelines) {
        vkDestroyPipeline(m_device, pipeline, nullptr);
    }
    if (m_vkCache != VK_NULL_HANDLE) {
        saveVkPipelineCache();
        vkDestroyPipelineCache(m_device, m_vkCache, nullptr);
        m_vkCache = VK_NULL_HANDLE;
    }
}

void PipelineCache::createVkPipelineCache() {
    const auto blob = tryReadCacheBlob(cacheFilePath());

    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    if (!blob.empty()) {
        info.initialDataSize = blob.size();
        info.pInitialData = blob.data();
    }

    // If the on-disk blob is from a different driver / GPU, the validation
    // header inside the blob mismatches and the driver returns
    // VK_ERROR_INCOMPATIBLE_DRIVER (or silently starts fresh, per spec). We
    // treat any failure as "no existing cache" and retry with an empty one.
    VkResult result = vkCreatePipelineCache(m_device, &info, nullptr, &m_vkCache);
    if (result != VK_SUCCESS && !blob.empty()) {
        std::cerr << "PipelineCache: discarding incompatible on-disk cache (VkResult "
                  << static_cast<int>(result) << ")" << std::endl;
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        result = vkCreatePipelineCache(m_device, &info, nullptr, &m_vkCache);
    }
    LOOM_VK_CHECK(result);
}

void PipelineCache::saveVkPipelineCache() const {
    const auto path = cacheFilePath();
    if (path.empty()) return;

    size_t dataSize = 0;
    if (vkGetPipelineCacheData(m_device, m_vkCache, &dataSize, nullptr) != VK_SUCCESS ||
        dataSize == 0) {
        return;
    }
    std::vector<uint8_t> blob(dataSize);
    if (vkGetPipelineCacheData(m_device, m_vkCache, &dataSize, blob.data()) != VK_SUCCESS) {
        return;
    }

    // Write to a sibling temp file then rename, so a crash mid-write leaves
    // the previous cache intact rather than truncated.
    const auto tmp = path;
    auto tmpPath = tmp;
    tmpPath += ".tmp";

    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return;
        file.write(reinterpret_cast<const char*>(blob.data()),
                   static_cast<std::streamsize>(dataSize));
        if (!file) return;
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) std::filesystem::remove(tmpPath, ec);
}

VkPipeline PipelineCache::getOrCreate(const std::string& spvPath) {
    if (m_pipelines.contains(spvPath)) {
        return m_pipelines[spvPath];
    }

    auto shaderCode = loadSpirv(spvPath);

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
    if (vkCreateComputePipelines(m_device, m_vkCache, 1, &pipelineInfo, nullptr, &pipeline) !=
        VK_SUCCESS) {
        vkDestroyShaderModule(m_device, shaderModule, nullptr);
        throw std::runtime_error("failed to create compute pipeline for: " + spvPath);
    }

    vkDestroyShaderModule(m_device, shaderModule, nullptr);

    m_pipelines[spvPath] = pipeline;
    return pipeline;
}

}  // namespace loom::gpu
