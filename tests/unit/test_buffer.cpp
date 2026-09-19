#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

#include "msaflow/buffer.hpp"

namespace msaflow {
namespace {

TEST(Buffer, AlignedAndSized) {
  Buffer buffer(4096);
  ASSERT_NE(buffer.data(), nullptr);
  EXPECT_EQ(buffer.size(), 4096u);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(buffer.data()) % 4096u, 0u);
}

TEST(Buffer, MoveTransfersOwnership) {
  Buffer first(2048);
  void* original = first.data();
  Buffer second(std::move(first));
  EXPECT_EQ(second.data(), original);
  EXPECT_EQ(second.size(), 2048u);
  EXPECT_EQ(first.data(), nullptr);
  EXPECT_EQ(first.size(), 0u);
}

}  // namespace
}  // namespace msaflow
