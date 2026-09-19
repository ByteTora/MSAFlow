#include <gtest/gtest.h>

#include "msaflow/block_aggregator.hpp"

namespace msaflow {
namespace {

TEST(BlockAggregator, SpecExample) {
  const std::vector<BlockRequest> requests = {
      {1, 100}, {2, 300}, {3, 100}, {4, 500}, {5, 300},
  };
  const ConsumerMap consumers = aggregate_requests(requests);
  ASSERT_EQ(consumers.size(), 3u);
  EXPECT_EQ(consumers.at(100), (std::vector<uint64_t>{1, 3}));
  EXPECT_EQ(consumers.at(300), (std::vector<uint64_t>{2, 5}));
  EXPECT_EQ(consumers.at(500), (std::vector<uint64_t>{4}));
}

TEST(BlockAggregator, EmptyInput) {
  EXPECT_TRUE(aggregate_requests({}).empty());
}

TEST(BlockAggregator, PreservesPerBlockRequestOrder) {
  const std::vector<BlockRequest> requests = {{5, 1}, {4, 1}, {3, 2}, {1, 1}};
  const ConsumerMap consumers = aggregate_requests(requests);
  EXPECT_EQ(consumers.at(1), (std::vector<uint64_t>{5, 4, 1}));
  EXPECT_EQ(consumers.at(2), (std::vector<uint64_t>{3}));
}

}  // namespace
}  // namespace msaflow
