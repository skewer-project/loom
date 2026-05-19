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
    EXPECT_EQ(ref.deep.layout, nullptr);
}

TEST(ResourceRefTest, FromDeepCarriesPayload) {
    gpu::ResourceRef::DeepRef d;
    d.countImage.poolIndex = 1;
    d.countImage.bindlessSlot = 11;
    d.countImage.generation = 1;
    d.offsetImage.poolIndex = 2;
    d.offsetImage.bindlessSlot = 12;
    d.offsetImage.generation = 1;
    d.samples.poolIndex = 3;
    d.samples.bindlessSlot = 13;
    d.samples.generation = 1;
    // The layout pointer in DeepRef is a non-owning reference into the
    // process-wide DeepLayoutRegistry; we leave it null here so this test
    // stays linkage-free of core::DeepLayout.
    d.layout = nullptr;
    d.width = 320;
    d.height = 240;
    d.totalSamples = 12345;

    auto ref = gpu::ResourceRef::fromDeep(d);
    EXPECT_EQ(ref.kind, gpu::ResourceRef::Kind::Deep);
    EXPECT_TRUE(ref.isValid());
    EXPECT_EQ(ref.deep.countImage.poolIndex, 1u);
    EXPECT_EQ(ref.deep.offsetImage.poolIndex, 2u);
    EXPECT_EQ(ref.deep.samples.poolIndex, 3u);
    EXPECT_FALSE(ref.deep.sampleToPixel.isValid());
    EXPECT_EQ(ref.deep.width, 320u);
    EXPECT_EQ(ref.deep.height, 240u);
    EXPECT_EQ(ref.deep.totalSamples, 12345u);
}
