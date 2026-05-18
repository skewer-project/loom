#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace loom::gpu {

// SPIR-V → VkPipeline cache backed by a real VkPipelineCache, persisted to
// disk between runs. On construction, attempts to load
// `<userDataDir>/pipeline_cache.bin`; on destruction, writes back whatever the
// driver produced. A missing or unreadable file is non-fatal: the engine
// boots with an empty cache and re-warms from shader sources.
//
// Two pipeline kinds are supported:
//
//   - **Compute** — `getOrCreate(spvPath)`. Uses the constructor-provided
//     global pipeline layout. Keyed by SPV path alone.
//   - **Graphics** — `getOrCreateGraphics(key)`. Each call site provides its
//     own layout and the small set of pipeline-state knobs Loom actually
//     varies. Keyed by the full `GraphicsPipelineKey`. Enum-permutation
//     keying is right-sized for a research compositor; if the matrix ever
//     explodes (many shader variants × blend modes × formats), the cache
//     remains correct but its memory footprint grows linearly.

// Vertex-input layout. v1 only ships `None` — every shader pulls data via
// `gl_VertexIndex` from a bindless SSBO. Future entries: `DeepSampleVertex`
// (per-sample point cloud with explicit vertex stride), `MeshVertex`.
enum class VertexInputDesc : uint8_t {
    None = 0,
};

// Primitive topology. PointCloudPass uses `PointList`; DisplayPass-style
// fullscreen-triangle passes use `TriangleList`.
enum class Topology : uint8_t {
    TriangleList = 0,
    PointList = 1,
};

// Color-blend mode of the (single) color attachment. Premultiplied
// straight-alpha blending is `AlphaBlend`; `Additive` is for accumulation.
enum class BlendMode : uint8_t {
    Opaque = 0,
    AlphaBlend = 1,
    Additive = 2,
};

// Depth-test / depth-write combination. `None` disables both;
// `TestNoWrite` is the convention for transparent geometry after the opaque
// pass.
enum class DepthMode : uint8_t {
    None = 0,
    TestWrite = 1,
    TestNoWrite = 2,
};

struct GraphicsPipelineKey {
    // Shader sources. Both required; geometry / tess stages not currently
    // supported (no v1 caller needs them).
    std::string vertSpv;
    std::string fragSpv;

    // Pipeline layout. Owned by the caller; the cache keys on the handle.
    // Two passes sharing the exact same layout handle dedup their
    // pipelines if every other field matches.
    VkPipelineLayout layout = VK_NULL_HANDLE;

    VertexInputDesc vertexInput = VertexInputDesc::None;
    Topology topology = Topology::TriangleList;
    BlendMode blend = BlendMode::Opaque;
    DepthMode depth = DepthMode::None;

    // Single color attachment. Dynamic-rendering (Vulkan 1.3) means we don't
    // need a `VkRenderPass` — the format is part of the pipeline key.
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;

    // Depth attachment format. Set to `VK_FORMAT_UNDEFINED` when depth =
    // None; required when depth is `TestWrite` or `TestNoWrite`.
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

    bool operator==(const GraphicsPipelineKey& other) const noexcept;
};

struct GraphicsPipelineKeyHash {
    size_t operator()(const GraphicsPipelineKey& k) const noexcept;
};

class PipelineCache {
  public:
    PipelineCache(VkDevice device, VkPipelineLayout pipelineLayout);
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    // Loads SPIR-V from disk, creates a VkShaderModule, creates the compute
    // pipeline against the global pipeline layout (and the persistent
    // VkPipelineCache), and caches the resulting VkPipeline by filename.
    [[nodiscard]] VkPipeline getOrCreate(const std::string& spvPath);

    // Graphics-pipeline overload. Loads vert + frag SPV, builds a pipeline
    // with the state described by `key`, caches the result keyed on the
    // full `key`. Shares the persistent `VkPipelineCache` with the compute
    // path so the on-disk blob warms both shader stage types.
    [[nodiscard]] VkPipeline getOrCreateGraphics(const GraphicsPipelineKey& key);

    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }

  private:
    VkDevice m_device;
    VkPipelineLayout m_pipelineLayout;
    VkPipelineCache m_vkCache = VK_NULL_HANDLE;
    std::unordered_map<std::string, VkPipeline> m_pipelines;
    std::unordered_map<GraphicsPipelineKey, VkPipeline, GraphicsPipelineKeyHash>
        m_graphicsPipelines;

    void createVkPipelineCache();
    void saveVkPipelineCache() const;
};

}  // namespace loom::gpu
