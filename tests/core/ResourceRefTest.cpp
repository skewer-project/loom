#include <gtest/gtest.h>

#include "gpu/ResourceHandles.hpp"

namespace gpu = loom::gpu;

TEST(ResourceRefTest, DefaultConstructedIsNone) {
    gpu::ResourceRef ref;
    EXPECT_EQ(ref.kind, gpu::ResourceRef::Kind::None);
    EXPECT_FALSE(ref.isValid());
}

TEST(ResourceRefTest, FromImageCarriesHandle) {
    gpu::ImageHandle img;
    img.poolIndex = 42;
    img.bindlessSlot = 100;
    img.generation = 2;

    auto ref = gpu::ResourceRef::fromImage(img);
    EXPECT_EQ(ref.kind, gpu::ResourceRef::Kind::Image);
    EXPECT_TRUE(ref.isValid());
    EXPECT_EQ(ref.image.poolIndex, 42u);
    EXPECT_EQ(ref.image.bindlessSlot, 100u);
    EXPECT_EQ(ref.image.generation, 2u);
}

TEST(ResourceRefTest, FromBufferCarriesHandle) {
    gpu::BufferHandle buf;
    buf.poolIndex = 7;
    buf.bindlessSlot = 13;
    buf.generation = 1;

    auto ref = gpu::ResourceRef::fromBuffer(buf);
    EXPECT_EQ(ref.kind, gpu::ResourceRef::Kind::Buffer);
    EXPECT_TRUE(ref.isValid());
    EXPECT_EQ(ref.buffer.poolIndex, 7u);
    EXPECT_EQ(ref.buffer.bindlessSlot, 13u);
    EXPECT_EQ(ref.buffer.generation, 1u);
}

TEST(ResourceRefTest, DeepSlotReservedButZeroByDefault) {
    // The Kind::Deep payload is wired into the union but no production code
    // populates it in this branch. Confirm the slot exists and that the inner
    // handles default to invalid.
    gpu::ResourceRef ref;
    ref.kind = gpu::ResourceRef::Kind::Deep;
    EXPECT_TRUE(ref.isValid());
    EXPECT_FALSE(ref.deep.countImage.isValid());
    EXPECT_FALSE(ref.deep.offsetImage.isValid());
    EXPECT_FALSE(ref.deep.samples.isValid());
}
