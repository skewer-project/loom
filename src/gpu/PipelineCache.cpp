#include "gpu/PipelineCache.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "platform/UserDataDir.hpp"

namespace loom::gpu {

bool GraphicsPipelineKey::operator==(const GraphicsPipelineKey& other) const noexcept {
    return vertSpv == other.vertSpv && fragSpv == other.fragSpv && layout == other.layout &&
           vertexInput == other.vertexInput && topology == other.topology && blend == other.blend &&
           depth == other.depth && colorFormat == other.colorFormat &&
           depthFormat == other.depthFormat && samples == other.samples;
}

size_t GraphicsPipelineKeyHash::operator()(const GraphicsPipelineKey& k) const noexcept {
    // boost::hash_combine pattern. The string hashes drive most of the
    // entropy; the enum / format fields slot in via the golden-ratio mix.
    size_t h = std::hash<std::string>{}(k.vertSpv);
    auto combine = [&h](size_t v) { h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2); };
    combine(std::hash<std::string>{}(k.fragSpv));
    combine(reinterpret_cast<uintptr_t>(k.layout));
    combine(static_cast<size_t>(k.vertexInput));
    combine(static_cast<size_t>(k.topology));
    combine(static_cast<size_t>(k.blend));
    combine(static_cast<size_t>(k.depth));
    combine(static_cast<size_t>(k.colorFormat));
    combine(static_cast<size_t>(k.depthFormat));
    combine(static_cast<size_t>(k.samples));
    return h;
}

namespace {

namespace fs = std::filesystem;

fs::path cacheFilePath() {
    try {
        return loom::platform::userDataDir() / "pipeline_cache.bin";
    } catch (const std::exception& e) {
        // If we can't resolve a user-data dir we degrade to an in-memory
        // cache for this run.
        loom::log::warn("PipelineCache: could not resolve user-data dir (", e.what(),
                        "); pipeline cache will not be persisted.");
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
    for (auto& [key, pipeline] : m_graphicsPipelines) {
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
        loom::log::warn("PipelineCache: discarding incompatible on-disk cache (VkResult ",
                        static_cast<int>(result), ")");
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

namespace {

VkPipelineColorBlendAttachmentState blendAttachmentFor(BlendMode mode) {
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    switch (mode) {
        case BlendMode::Opaque:
            att.blendEnable = VK_FALSE;
            break;
        case BlendMode::AlphaBlend:
            // Premultiplied "over": Cdst = Csrc + (1 - Asrc) * Cdst.
            att.blendEnable = VK_TRUE;
            att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            att.colorBlendOp = VK_BLEND_OP_ADD;
            att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            att.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BlendMode::Additive:
            att.blendEnable = VK_TRUE;
            att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            att.colorBlendOp = VK_BLEND_OP_ADD;
            att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            att.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }
    return att;
}

VkPipelineDepthStencilStateCreateInfo depthStateFor(DepthMode mode) {
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    switch (mode) {
        case DepthMode::None:
            ds.depthTestEnable = VK_FALSE;
            ds.depthWriteEnable = VK_FALSE;
            break;
        case DepthMode::TestWrite:
            ds.depthTestEnable = VK_TRUE;
            ds.depthWriteEnable = VK_TRUE;
            ds.depthCompareOp = VK_COMPARE_OP_LESS;
            break;
        case DepthMode::TestNoWrite:
            ds.depthTestEnable = VK_TRUE;
            ds.depthWriteEnable = VK_FALSE;
            ds.depthCompareOp = VK_COMPARE_OP_LESS;
            break;
    }
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;
    return ds;
}

}  // namespace

VkPipeline PipelineCache::getOrCreateGraphics(const GraphicsPipelineKey& key) {
    if (auto it = m_graphicsPipelines.find(key); it != m_graphicsPipelines.end()) {
        return it->second;
    }

    auto vertCode = loadSpirv(key.vertSpv);
    auto fragCode = loadSpirv(key.fragSpv);

    auto makeModule = [this](const std::vector<char>& code) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule m = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_device, &info, nullptr, &m) != VK_SUCCESS) {
            throw std::runtime_error("PipelineCache: vkCreateShaderModule failed");
        }
        return m;
    };
    VkShaderModule vertModule = makeModule(vertCode);
    VkShaderModule fragModule = makeModule(fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // v1 vertex-input matrix: only `None` is wired. Every shader pulls data
    // via gl_VertexIndex from an SSBO; there's no per-vertex attribute
    // binding to describe. Future entries (DeepSampleVertex, MeshVertex)
    // would populate this struct from an enum-keyed switch.
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = (key.topology == Topology::PointList) ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                                                        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ia.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = key.samples;

    auto blendAtt = blendAttachmentFor(key.blend);
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blendAtt;

    auto ds = depthStateFor(key.depth);

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &key.colorFormat;
    rendering.depthAttachmentFormat = key.depthFormat;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = key.layout;
    info.renderPass = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(m_device, m_vkCache, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("PipelineCache: vkCreateGraphicsPipelines failed");
    }

    m_graphicsPipelines[key] = pipeline;
    return pipeline;
}

}  // namespace loom::gpu
