#include <gtest/gtest.h>

#include <cstring>
#include <type_traits>

#include "gpu/ComputeTask.hpp"

namespace gpu = loom::gpu;

namespace {

// Trivially copyable POD payload that fits the 128-byte budget.
struct WellFormedPC {
    float color[4];
    uint32_t outputSlot;
    uint32_t width;
    uint32_t height;
    uint32_t _pad;
};
static_assert(sizeof(WellFormedPC) <= gpu::MAX_PUSH_CONSTANT_BYTES);
static_assert(std::is_trivially_copyable_v<WellFormedPC>);

// A payload right at the 128-byte boundary.
struct MaximumPC {
    uint64_t data[16];  // 128 bytes exactly
};
static_assert(sizeof(MaximumPC) == gpu::MAX_PUSH_CONSTANT_BYTES);

}  // namespace

TEST(PushConstantTest, SetCopiesBytesVerbatim) {
    gpu::ComputeTask task{};

    WellFormedPC pc{};
    pc.color[0] = 1.0f;
    pc.color[1] = 0.5f;
    pc.color[2] = 0.25f;
    pc.color[3] = 1.0f;
    pc.outputSlot = 42;
    pc.width = 1280;
    pc.height = 720;

    task.setPushConstants(pc);

    EXPECT_EQ(task.pushConstantSize, sizeof(WellFormedPC));

    WellFormedPC readback{};
    std::memcpy(&readback, task.pushConstants.data(), sizeof(WellFormedPC));
    EXPECT_FLOAT_EQ(readback.color[0], 1.0f);
    EXPECT_FLOAT_EQ(readback.color[1], 0.5f);
    EXPECT_FLOAT_EQ(readback.color[2], 0.25f);
    EXPECT_FLOAT_EQ(readback.color[3], 1.0f);
    EXPECT_EQ(readback.outputSlot, 42u);
    EXPECT_EQ(readback.width, 1280u);
    EXPECT_EQ(readback.height, 720u);
}

TEST(PushConstantTest, MaximumBudgetIsAccepted) {
    // Right at the 128-byte boundary the helper still compiles and succeeds
    // at runtime — the static_assert is `<=`, not `<`.
    gpu::ComputeTask task{};
    MaximumPC pc{};
    for (size_t i = 0; i < 16; ++i) pc.data[i] = static_cast<uint64_t>(i) << 32;
    task.setPushConstants(pc);
    EXPECT_EQ(task.pushConstantSize, sizeof(MaximumPC));
    EXPECT_EQ(task.pushConstantSize, gpu::MAX_PUSH_CONSTANT_BYTES);
}

// Negative compile-time check. Guarded so the normal build is clean; flip
// `LOOM_TEST_NEGATIVE_COMPILES` to verify the static_assert fires for a 129-
// byte payload.
//
//     cmake --build build -DLOOM_TEST_NEGATIVE_COMPILES=1
//
// Expected: compile failure on the line tagged with the static_assert
// message "push-constant payload exceeds the 128-byte spec minimum".
#ifdef LOOM_TEST_NEGATIVE_COMPILES
struct OverflowingPC {
    uint8_t data[129];
};
inline void shouldFailToCompile() {
    gpu::ComputeTask task{};
    OverflowingPC pc{};
    task.setPushConstants(pc);
}
#endif
