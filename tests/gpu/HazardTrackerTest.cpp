#include <gtest/gtest.h>

#include <array>

#include "gpu/ComputeTask.hpp"
#include "gpu/HazardTracker.hpp"

namespace gpu = loom::gpu;

namespace {

gpu::ImageHandle makeHandle(uint32_t poolIndex, uint32_t bindlessSlot, uint32_t generation) {
    gpu::ImageHandle h;
    h.poolIndex = poolIndex;
    h.bindlessSlot = bindlessSlot;
    h.generation = generation;
    return h;
}

gpu::ComputeTask makeTask(std::vector<gpu::ImageHandle> reads,
                          std::vector<gpu::ImageHandle> writes) {
    gpu::ComputeTask task{};
    task.readDependencies = std::move(reads);
    task.writeDependencies = std::move(writes);
    return task;
}

}  // namespace

TEST(HazardTrackerTest, RAWBarrierEmitted) {
    gpu::HazardTracker t;
    t.beginFrame();

    const auto h = makeHandle(0, 100, 1);

    t.recordTask(makeTask({}, {h}));

    const std::array<gpu::ImageHandle, 1> reads{h};
    EXPECT_TRUE(t.needsBarrierBeforeRead(reads));
}

TEST(HazardTrackerTest, WAWBarrierEmitted) {
    gpu::HazardTracker t;
    t.beginFrame();

    const auto h = makeHandle(0, 100, 1);

    t.recordTask(makeTask({}, {h}));

    const std::array<gpu::ImageHandle, 1> writes{h};
    EXPECT_TRUE(t.needsBarrierBeforeWrite(writes));
}

TEST(HazardTrackerTest, IndependentNoBarrier) {
    gpu::HazardTracker t;
    t.beginFrame();

    const auto a = makeHandle(0, 100, 1);
    const auto b = makeHandle(1, 200, 1);

    t.recordTask(makeTask({}, {a}));

    const std::array<gpu::ImageHandle, 1> writes{b};
    const std::array<gpu::ImageHandle, 1> reads{b};
    EXPECT_FALSE(t.needsBarrierBeforeWrite(writes));
    EXPECT_FALSE(t.needsBarrierBeforeRead(reads));
}

TEST(HazardTrackerTest, BarrierResetsTracking) {
    gpu::HazardTracker t;
    t.beginFrame();

    const auto h = makeHandle(0, 100, 1);

    t.recordTask(makeTask({}, {h}));
    t.clearAfterBarrier();

    // After clear, the next read of the same slot does not re-trigger until
    // a new write is recorded.
    const std::array<gpu::ImageHandle, 1> reads{h};
    EXPECT_FALSE(t.needsBarrierBeforeRead(reads));
}

TEST(HazardTrackerTest, GenerationDisambiguates) {
    // A slot recycled to a new generation must not collide with the prior
    // generation's write — without this, slot reuse would mask real hazards.
    gpu::HazardTracker t;
    t.beginFrame();

    const auto gen1 = makeHandle(0, 100, 1);
    const auto gen2 = makeHandle(0, 100, 2);  // same poolIndex, different gen

    t.recordTask(makeTask({}, {gen1}));

    const std::array<gpu::ImageHandle, 1> readsGen2{gen2};
    EXPECT_FALSE(t.needsBarrierBeforeRead(readsGen2));
}

TEST(HazardTrackerTest, WARScaffoldReturnsFalseInV1) {
    // v1 has no node that reads then writes the same slot inside a frame;
    // the read set is tracked but the predicate is a no-op. Flipping this
    // behaviour is a one-line change.
    gpu::HazardTracker t;
    t.beginFrame();

    const auto h = makeHandle(0, 100, 1);

    t.recordTask(makeTask({h}, {}));

    const std::array<gpu::ImageHandle, 1> writes{h};
    EXPECT_FALSE(t.needsBarrierBeforeWriteAfterRead(writes));
}

TEST(HazardTrackerTest, InvalidHandlesIgnored) {
    gpu::HazardTracker t;
    t.beginFrame();

    gpu::ImageHandle invalid;  // default-constructed -> isValid() == false
    const auto valid = makeHandle(0, 100, 1);

    t.recordTask(makeTask({}, {invalid, valid}));

    const std::array<gpu::ImageHandle, 1> readsInvalid{invalid};
    EXPECT_FALSE(t.needsBarrierBeforeRead(readsInvalid));

    const std::array<gpu::ImageHandle, 1> readsValid{valid};
    EXPECT_TRUE(t.needsBarrierBeforeRead(readsValid));
}
