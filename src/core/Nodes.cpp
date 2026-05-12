#include "core/Nodes.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

#include "core/DeepExrLoader.hpp"
#include "core/EvaluationContext.hpp"
#include "core/Graph.hpp"
#include "core/RenderCache.hpp"
#include "gpu/BindlessHeap.hpp"
#include "gpu/ComputeTask.hpp"
#include "gpu/PipelineCache.hpp"
#include "gpu/TransientBufferPool.hpp"
#include "gpu/TransientImagePool.hpp"
#include "gpu/VulkanContext.hpp"

namespace loom::core {

gpu::ImageHandle Node::pullInput(EvaluationContext& ctx, uint32_t inputIndex) {
    ...
