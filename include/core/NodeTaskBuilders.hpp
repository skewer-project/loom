#pragma once

#include "core/EvaluationContext.hpp"
#include "gpu/ComputeTask.hpp"
#include "gpu/ResourceHandles.hpp"

namespace loom::core {

// Records a Fill.comp dispatch that writes `color` into `out`. Used by
// ConstantNode and as the no-input fallback for MergeNode / PassthroughNode.
// `label` is borrowed (string literal) so callers can attribute the dispatch
// in RenderDoc / validation output.
gpu::ComputeTask buildFillTask(EvaluationContext& ctx, gpu::ImageHandle out, const float color[4],
                               const char* label);

// Records a Passthrough.comp dispatch copying `in` to `out`.
gpu::ComputeTask buildPassthroughTask(EvaluationContext& ctx, gpu::ImageHandle in,
                                      gpu::ImageHandle out, const char* label);

// Records a DeepFlatten.comp dispatch that walks the deep payload's samples
// and composites them front-to-back into `out`. v1 supports two layouts
// (both alphabetical `A B G R Z ZBack`): half-RGBA stride 16 and
// float-RGBA stride 24. `rgbaIsFloat` picks which path the shader takes;
// the caller derives it from the layout (see `detectV1FlattenFormat`).
gpu::ComputeTask buildDeepFlattenTask(EvaluationContext& ctx, const gpu::ResourceRef::DeepRef& src,
                                      gpu::ImageHandle out, bool rgbaIsFloat, const char* label);

}  // namespace loom::core
