#include <gtest/gtest.h>

#include <unordered_set>

#include "msaflow/types.hpp"

namespace msaflow {
namespace {

TEST(Types, DatabaseMetaDefaults) {
  DatabaseMeta meta;
  EXPECT_EQ(meta.sequence_count, 0u);
  EXPECT_EQ(meta.block_count, 0u);
  EXPECT_TRUE(meta.checksum.empty());
}

TEST(Types, QueryStateDefaults) {
  QueryState query;
  EXPECT_EQ(query.status, QueryStatus::CREATED);
  EXPECT_EQ(query.current_block, 0u);
  EXPECT_EQ(query.blocks_processed, 0u);
}

TEST(Types, BlockRuntimeDefaults) {
  BlockRuntime block;
  EXPECT_EQ(block.state, BlockState::ABSENT);
  EXPECT_EQ(block.cache_class, CacheClass::NONE);
  EXPECT_EQ(block.active_consumers, 0u);
  EXPECT_EQ(block.buffer, nullptr);
}

TEST(Types, CacheKeyEquality) {
  CacheKey a{{1}, {2, 3}, 4};
  CacheKey b{{1}, {2, 3}, 4};
  CacheKey other_block{{1}, {2, 3}, 5};
  CacheKey other_version{{1}, {2, 4}, 4};
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a == other_block);
  EXPECT_FALSE(a == other_version);
}

TEST(Types, CacheKeyHashDistinguishesVersions) {
  std::unordered_set<CacheKey, CacheKeyHash> keys;
  keys.insert(CacheKey{{1}, {2, 3}, 4});
  keys.insert(CacheKey{{1}, {2, 3}, 4});
  keys.insert(CacheKey{{1}, {2, 4}, 4});
  keys.insert(CacheKey{{1}, {2, 3}, 5});
  EXPECT_EQ(keys.size(), 3u);
}

}  // namespace
}  // namespace msaflow
