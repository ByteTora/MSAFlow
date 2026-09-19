#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "msaflow/buffer_pool.hpp"

namespace msaflow {
namespace {

TEST(BufferPool, ReportsCapacityAndFreeCount) {
  BufferPool pool(1000, 3);
  EXPECT_EQ(pool.capacity(), 3u);
  EXPECT_EQ(pool.free_count(), 3u);
  EXPECT_EQ(pool.buffer_bytes(), 1000u);
}

TEST(BufferPool, BuffersAre4KAligned) {
  BufferPool pool(100, 2);
  Buffer* buffer = pool.acquire();
  ASSERT_NE(buffer, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(buffer->data()) % 4096, 0u);
  pool.release(buffer);
}

TEST(BufferPool, ExhaustionReturnsNullAndReleaseRestores) {
  BufferPool pool(64, 2);
  Buffer* first = pool.acquire();
  Buffer* second = pool.acquire();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(pool.acquire(), nullptr);
  EXPECT_EQ(pool.free_count(), 0u);

  pool.release(first);
  EXPECT_EQ(pool.free_count(), 1u);
  Buffer* reused = pool.acquire();
  EXPECT_EQ(reused, first);
  pool.release(reused);
  pool.release(second);
  EXPECT_EQ(pool.free_count(), 2u);
}

}  // namespace
}  // namespace msaflow
