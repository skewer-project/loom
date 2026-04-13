#include <gtest/gtest.h>

#include "core/Types.hpp"

using namespace loom::core;

TEST(IdTagTest, EncodeAndDecodeNode) {
    uint32_t index = 42;
    uint64_t encoded = encodeId(index, IdTag::Node);

    // Check tag bits for Node (0x00000000)
    EXPECT_EQ(encoded & 0xC0000000, 0);
    EXPECT_EQ(decodeIndex(encoded), index);
}

TEST(IdTagTest, EncodeAndDecodePin) {
    uint32_t index = 123456789;
    uint64_t encoded = encodeId(index, IdTag::Pin);

    // Check tag bits for Pin (0x40000000)
    EXPECT_EQ(encoded & 0xC0000000, 0x40000000);
    EXPECT_EQ(decodeIndex(encoded), index);
}

TEST(IdTagTest, EncodeAndDecodeLink) {
    uint32_t index = 0x1FFFFFFF;
    uint64_t encoded = encodeId(index, IdTag::Link);

    // Check tag bits for Link (0x80000000)
    EXPECT_EQ(encoded & 0xC0000000, 0x80000000);
    EXPECT_EQ(decodeIndex(encoded), index);
}
TEST(IdTagTest, Partitioning) {
    uint32_t index = 1;
    uint64_t nodeId = encodeId(index, IdTag::Node);
    uint64_t pinId = encodeId(index, IdTag::Pin);
    uint64_t linkId = encodeId(index, IdTag::Link);

    EXPECT_NE(nodeId, pinId);
    EXPECT_NE(nodeId, linkId);
    EXPECT_NE(pinId, linkId);

    EXPECT_EQ(decodeIndex(nodeId), index);
    EXPECT_EQ(decodeIndex(pinId), index);
    EXPECT_EQ(decodeIndex(linkId), index);
}
