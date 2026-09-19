#include <gtest/gtest.h>

#include "msaflow/cache.hpp"

namespace msaflow {
namespace {

CacheKey key(uint64_t block_id) { return CacheKey{{1}, {1, 0}, block_id}; }

TEST(CacheManager, AdmitLookupAndHits) {
  CacheManager cache(CacheConfig{4, 0.9, 0.75});
  EXPECT_EQ(cache.lookup(key(0)), nullptr);
  EXPECT_TRUE(cache.admit(key(0), CacheClass::SHARED, 10));
  ASSERT_NE(cache.lookup(key(0)), nullptr);
  EXPECT_FALSE(cache.admit(key(0), CacheClass::SHARED, 11));
  EXPECT_EQ(cache.lookup(key(9)), nullptr);
  EXPECT_EQ(cache.stats().admissions, 1u);
  EXPECT_EQ(cache.stats().hits, 1u);
  EXPECT_EQ(cache.occupancy(), 1u);
}

TEST(CacheManager, StreamingReleasedAfterLastConsumer) {
  CacheManager cache(CacheConfig{4, 0.9, 0.75});
  ASSERT_TRUE(cache.admit(key(0), CacheClass::STREAMING, 10));
  ASSERT_TRUE(cache.acquire(key(0)));
  ASSERT_EQ(cache.occupancy(), 1u);
  ASSERT_TRUE(cache.release(key(0), 20));
  EXPECT_EQ(cache.occupancy(), 0u);
  EXPECT_EQ(cache.lookup(key(0)), nullptr);
  EXPECT_EQ(cache.stats().streaming_released, 1u);
}

TEST(CacheManager, SharedRetainedAfterLastConsumer) {
  CacheManager cache(CacheConfig{4, 0.9, 0.75});
  ASSERT_TRUE(cache.admit(key(0), CacheClass::SHARED, 10));
  ASSERT_TRUE(cache.acquire(key(0)));
  ASSERT_TRUE(cache.release(key(0), 20));
  EXPECT_EQ(cache.occupancy(), 1u);
  EXPECT_NE(cache.lookup(key(0)), nullptr);
}

TEST(CacheManager, EvictionRespectsLowWatermark) {
  CacheManager cache(CacheConfig{10, 0.9, 0.75});
  for (uint64_t block = 0; block < 10; ++block) {
    ASSERT_TRUE(cache.admit(key(block), CacheClass::SHARED, block));
  }
  cache.evict_to_low_watermark(1000);
  EXPECT_EQ(cache.occupancy(), 7u);
  EXPECT_EQ(cache.stats().evictions, 3u);
}

TEST(CacheManager, PinnedEntriesSurviveEviction) {
  CacheManager cache(CacheConfig{10, 0.9, 0.75});
  for (uint64_t block = 0; block < 10; ++block) {
    ASSERT_TRUE(cache.admit(key(block), CacheClass::SHARED, block));
  }
  for (uint64_t block = 1; block < 10; ++block) {
    ASSERT_TRUE(cache.acquire(key(block)));
  }
  cache.evict_to_low_watermark(1000);
  EXPECT_EQ(cache.occupancy(), 9u);
  EXPECT_EQ(cache.lookup(key(0)), nullptr);
  EXPECT_NE(cache.lookup(key(1)), nullptr);
}

TEST(CacheManager, KeepScoreWeightsConsumersAndRecency) {
  CacheManager cache(CacheConfig{10, 0.9, 0.75});
  ASSERT_TRUE(cache.admit(key(0), CacheClass::SHARED, 0));
  ASSERT_TRUE(cache.admit(key(1), CacheClass::SHARED, 0));
  ASSERT_TRUE(cache.admit(key(2), CacheClass::SHARED, 0));
  ASSERT_TRUE(cache.acquire(key(0)));
  ASSERT_TRUE(cache.acquire(key(0)));
  ASSERT_TRUE(cache.acquire(key(0)));
  cache.set_future_consumers(key(1), 4);
  const double pinned = cache.keep_score(key(0), 0);
  const double wanted = cache.keep_score(key(1), 0);
  const double idle = cache.keep_score(key(2), 0);
  EXPECT_GT(pinned, idle);
  EXPECT_GT(wanted, idle);
  EXPECT_GT(cache.keep_score(key(0), 0), cache.keep_score(key(0), 1000));
}

}  // namespace
}  // namespace msaflow
