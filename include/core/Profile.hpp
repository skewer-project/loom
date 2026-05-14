#pragma once

// Loom profiling-scope macros.
//
// v1: compile-time no-op when `LOOM_ENABLE_PROFILING` is undefined.
// v2: planned switch to Tracy — `LOOM_PROFILE_SCOPE(name)` and
//     `LOOM_PROFILE_FRAME()` map onto `ZoneScopedN(name)` and `FrameMark`
//     respectively in `Profile.cpp` / a future Tracy adapter, with no churn
//     at call sites.
//
// Instrument anything plausibly above microsecond cost:
//   Graph::execute, DispatchManager::submit, DisplayPass::record,
//   FrameLoop::beginFrame/endFrame, shader compilation, pipeline creation.

#define LOOM_PROFILE_CONCAT_INNER(a, b) a##b
#define LOOM_PROFILE_CONCAT(a, b) LOOM_PROFILE_CONCAT_INNER(a, b)

#if defined(LOOM_ENABLE_PROFILING)

namespace loom::profile {

class ScopedSample {
  public:
    explicit ScopedSample(const char* name);
    ~ScopedSample();
    ScopedSample(const ScopedSample&) = delete;
    ScopedSample& operator=(const ScopedSample&) = delete;
};

void frameMark();

}  // namespace loom::profile

#define LOOM_PROFILE_SCOPE(name) \
    ::loom::profile::ScopedSample LOOM_PROFILE_CONCAT(_loom_sample_, __LINE__)(name)
#define LOOM_PROFILE_FRAME() ::loom::profile::frameMark()

#else

#define LOOM_PROFILE_SCOPE(name) ((void)0)
#define LOOM_PROFILE_FRAME() ((void)0)

#endif
