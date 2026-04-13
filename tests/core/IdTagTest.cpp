#include <gtest/gtest.h>

#include "core/Types.hpp"

using namespace loom::core;

TEST(IdTagTest, EncodeAndDecodeNode) {
    uint32_t index = 42;
    uint64_t encoded = encodeId(index, IdTag::Node);

    // Check top 2 bits for Node (00)
    EXPECT_EQ(encoded >> 62, 0);
    EXPECT_EQ(decodeIndex(encoded), index);
}

TEST(IdTagTest, EncodeAndDecodePin) {
    uint32_t index = 123456789;
    uint64_t encoded = encodeId(index, IdTag::Pin);

    // Check top 2 bits for Pin (01)
    EXPECT_EQ(encoded >> 62, 1);
    EXPECT_EQ(decodeIndex(encoded), index);
}

TEST(IdTagTest, EncodeAndDecodeLink) {
    uint32_t index = 0xFFFFFFFF;
    uint64_t encoded = encodeId(index, IdTag::Link);

    // Check top 2 bits for Link (10)
    EXPECT_EQ(encoded >> 62, 2);
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
