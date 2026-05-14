#include "core/NodeTaskBuilders.hpp"

#include "gpu/PipelineCache.hpp"

namespace loom::core {

namespace {

inline uint32_t groupCount(uint32_t pixels) { return (pixels + 15) / 16; }

}  // namespace

gpu::ComputeTask buildFillTask(EvaluationContext& ctx, gpu::ImageHandle out, const float color[4],
                               const char* label) {
    gpu::ComputeTask task{};
    task.label = label;
    task.pipeline = ctx.pipelineCache->getOrCreate("Fill.comp.spv");

    struct FillPC {
        float color[4];
        uint32_t outputSlot;
        uint32_t width;
        uint32_t height;
    } pc{};
    pc.color[0] = color[0];
    pc.color[1] = color[1];
    pc.color[2] = color[2];
    pc.color[3] = color[3];
    pc.outputSlot = out.bindlessSlot;
    pc.width = ctx.requestedExtent.width;
    pc.height = ctx.requestedExtent.height;
    task.setPushConstants(pc);

    task.groupCountX = groupCount(ctx.requestedExtent.width);
    task.groupCountY = groupCount(ctx.requestedExtent.height);
    task.groupCountZ = 1;
    task.writeDependencies.push_back(out);
    return task;
}

gpu::ComputeTask buildPassthroughTask(EvaluationContext& ctx, gpu::ImageHandle in,
                                      gpu::ImageHandle out, const char* label) {
    gpu::ComputeTask task{};
    task.label = label;
    task.pipeline = ctx.pipelineCache->getOrCreate("Passthrough.comp.spv");

    struct PassthroughPC {
        uint32_t inputSlot;
        uint32_t outputSlot;
        uint32_t width;
        uint32_t height;
    } pc{};
    pc.inputSlot = in.bindlessSlot;
    pc.outputSlot = out.bindlessSlot;
    pc.width = ctx.requestedExtent.width;
    pc.height = ctx.requestedExtent.height;
    task.setPushConstants(pc);

    task.groupCountX = groupCount(ctx.requestedExtent.width);
    task.groupCountY = groupCount(ctx.requestedExtent.height);
    task.groupCountZ = 1;
    task.readDependencies.push_back(in);
    task.writeDependencies.push_back(out);
    return task;
}

}  // namespace loom::core
