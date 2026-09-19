#include <gtest/gtest.h>

#include "msaflow/priority_policy.hpp"

namespace msaflow {
namespace {

PriorityInput input(uint32_t consumers, uint64_t wait_ns, uint64_t io_cost_ns, uint64_t arrival_seq,
                    uint64_t last_access_ns) {
  PriorityInput in;
  in.active_consumers = consumers;
  in.wait_ns = wait_ns;
  in.io_cost_ns = io_cost_ns;
  in.arrival_seq = arrival_seq;
  in.last_access_ns = last_access_ns;
  return in;
}

TEST(PriorityPolicy, SharingScore) {
  EXPECT_DOUBLE_EQ(sharing_score(0), 0.0);
  EXPECT_DOUBLE_EQ(sharing_score(1), 1.0);
  EXPECT_DOUBLE_EQ(sharing_score(3), 2.0);
}

TEST(PriorityPolicy, FifoPrefersEarlierArrival) {
  const double early = priority_score(PolicyKind::FIFO, input(1, 0, 0, 1, 0));
  const double late = priority_score(PolicyKind::FIFO, input(1, 0, 0, 2, 0));
  EXPECT_GT(early, late);
}

TEST(PriorityPolicy, LruPrefersOlderAccess) {
  const double older = priority_score(PolicyKind::LRU, input(1, 0, 0, 1, 100));
  const double newer = priority_score(PolicyKind::LRU, input(1, 0, 0, 1, 200));
  EXPECT_GT(older, newer);
}

TEST(PriorityPolicy, SharingOnlyIgnoresWaitAndIo) {
  const double many = priority_score(PolicyKind::SHARING_ONLY, input(4, 10, 999, 1, 0));
  const double few = priority_score(PolicyKind::SHARING_ONLY, input(1, 1000000000, 0, 2, 0));
  EXPECT_GT(many, few);
}

TEST(PriorityPolicy, UrgencyOnlyPrefersLongerWait) {
  const double waited = priority_score(PolicyKind::URGENCY_ONLY, input(1, 5000, 0, 1, 0));
  const double fresh = priority_score(PolicyKind::URGENCY_ONLY, input(1, 100, 0, 2, 0));
  EXPECT_GT(waited, fresh);
}

TEST(PriorityPolicy, MsaflowV0CombinesSharingUrgencyAndIoCost) {
  const PolicyParams params;
  const double base = priority_score(PolicyKind::MSAFLOW_V0, input(1, 1000, 1000, 1, 0), params);
  const double shared = priority_score(PolicyKind::MSAFLOW_V0, input(4, 1000, 1000, 1, 0), params);
  const double urgent = priority_score(PolicyKind::MSAFLOW_V0, input(1, 2000, 1000, 1, 0), params);
  const double costly = priority_score(PolicyKind::MSAFLOW_V0, input(1, 1000, 1000000, 1, 0), params);
  EXPECT_GT(shared, base);
  EXPECT_GT(urgent, base);
  EXPECT_LT(costly, base);
}

TEST(PriorityPolicy, AgingOutweighsMaximumSharing) {
  const double aged = priority_score(PolicyKind::MSAFLOW_V0, input(1, 1000000000ULL, 0, 1, 0));
  const double fresh = priority_score(PolicyKind::MSAFLOW_V0, input(64, 0, 0, 2, 0));
  EXPECT_GT(aged, fresh);
}

TEST(PriorityPolicy, ParamsControlWeights) {
  PolicyParams sharing_off;
  sharing_off.sharing_weight = 0.0;
  const double one = priority_score(PolicyKind::MSAFLOW_V0, input(1, 1000, 1000, 1, 0), sharing_off);
  const double four = priority_score(PolicyKind::MSAFLOW_V0, input(4, 1000, 1000, 1, 0), sharing_off);
  EXPECT_DOUBLE_EQ(one, four);
}

}  // namespace
}  // namespace msaflow
