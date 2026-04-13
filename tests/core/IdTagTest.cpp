#include <gtest/gtest.h>

#include "core/Types.hpp"

using namespace loom::core;

TEST(IdTagTest, EncodeAndDecodeNode) {
    uint32_t index = 42;
    uint32_t generation = 10;
    uint64_t encoded = encodeId(index, generation, IdTag::Node);

    // Check bits 62-63 for Node (00)
    EXPECT_EQ(encoded >> 62, 0);
    // Check generation bits 32-61
    EXPECT_EQ((encoded >> 32) & 0x3FFFFFFF, generation);
    EXPECT_EQ(decodeIndex(encoded), index);
}

TEST(IdTagTest, EncodeAndDecodePin) {
    uint32_t index = 123456789;
    uint32_t generation = 5;
    uint64_t encoded = encodeId(index, generation, IdTag::Pin);

    // Check bits 62-63 for Pin (01)
    EXPECT_EQ(encoded >> 62, 1);
    // Check generation bits 32-61
    EXPECT_EQ((encoded >> 32) & 0x3FFFFFFF, generation);
    EXPECT_EQ(decodeIndex(encoded), index);
}

TEST(IdTagTest, EncodeAndDecodeLink) {
    uint32_t index = 0x7FFFFFFF;
    uint32_t generation = 0x3FFFFFFF;
    uint64_t encoded = encodeId(index, generation, IdTag::Link);

    // Check bits 62-63 for Link (10)
    EXPECT_EQ(encoded >> 62, 2);
    // Check generation bits 32-61
    EXPECT_EQ((encoded >> 32) & 0x3FFFFFFF, generation);
    EXPECT_EQ(decodeIndex(encoded), index);
}

TEST(IdTagTest, Partitioning) {
    uint32_t index = 1;
    uint32_t gen = 1;
    uint64_t nodeId = encodeId(index, gen, IdTag::Node);
    uint64_t pinId = encodeId(index, gen, IdTag::Pin);
    uint64_t linkId = encodeId(index, gen, IdTag::Link);

    EXPECT_NE(nodeId, pinId);
    EXPECT_NE(nodeId, linkId);
    EXPECT_NE(pinId, linkId);

    EXPECT_EQ(decodeIndex(nodeId), index);
    EXPECT_EQ(decodeIndex(pinId), index);
    EXPECT_EQ(decodeIndex(linkId), index);
}
