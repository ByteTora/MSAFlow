#include <gtest/gtest.h>

#include <cstdint>

#include "des.hpp"
#include "msaflow/metrics.hpp"

namespace msaflow {
namespace {

Trace tight_trace(int queries) {
  Trace trace;
  trace.config.num_blocks = 8;
  trace.config.block_bytes = 1000;
  for (int q = 0; q < queries; ++q) {
    TraceQuery query;
    query.query_id = static_cast<uint64_t>(q);
    query.arrival_ns = static_cast<uint64_t>(q) * 10;
    TraceStream stream;
    stream.first_block_ns = query.arrival_ns;
    stream.block_interval_ns = 1000;
    stream.blocks = {0, 1};
    query.streams.push_back(stream);
    trace.queries.push_back(query);
  }
  return trace;
}

SimOptions base_options() {
  SimOptions options;
  options.block_bytes = 1000;
  options.bandwidth_bytes_per_ns = 1.0;
  options.base_latency_ns = 10;
  options.io_depth = 8;
  options.dram_blocks = 8;
  options.starvation_threshold_ns = 1000000000000ULL;
  return options;
}

SimConfig to_config(const SimOptions& options) {
  SimConfig config;
  config.policy = options.policy_name;
  config.dram_blocks = options.dram_blocks;
  config.block_bytes = options.block_bytes;
  config.io_depth = options.io_depth;
  config.prefetch = options.prefetch;
  return config;
}

TEST(Des, ThreeQueriesCoalesceOnSharedBlocks) {
  const SimMetrics metrics = run_simulation(tight_trace(3), base_options());
  EXPECT_EQ(metrics.queries, 3u);
  EXPECT_EQ(metrics.logical_block_requests, 6u);
  EXPECT_EQ(metrics.physical_block_reads, 2u);
  EXPECT_NEAR(coalescing_ratio(metrics), 1.0 - 2.0 / 6.0, 1e-9);
  EXPECT_EQ(metrics.coalesced_requests, 4u);
  EXPECT_EQ(metrics.starvation_count, 0u);
}

TEST(Des, SingleQueryHasNoReuse) {
  SimOptions options = base_options();
  Trace trace;
  trace.config.num_blocks = 3;
  TraceQuery query;
  query.query_id = 0;
  TraceStream stream;
  stream.block_interval_ns = 100000;
  stream.blocks = {0, 1, 2};
  query.streams.push_back(stream);
  trace.queries.push_back(query);
  const SimMetrics metrics = run_simulation(trace, options);
  EXPECT_EQ(metrics.physical_block_reads, 3u);
  EXPECT_NEAR(coalescing_ratio(metrics), 0.0, 1e-9);
  EXPECT_EQ(metrics.shared_cache_hits, 0u);
}

TEST(Des, PrefetchIsIssuedAndConsumed) {
  SimOptions options = base_options();
  options.prefetch = true;
  const SimMetrics metrics = run_simulation(tight_trace(3), options);
  EXPECT_GE(metrics.prefetch_issued, 1u);
  EXPECT_GE(metrics.prefetch_hits, 1u);
  EXPECT_EQ(metrics.prefetch_waste, 0u);
  EXPECT_EQ(metrics.physical_block_reads, 2u);
}

TEST(Des, DrainsPendingBacklogForDisjointBlocks) {
  SimOptions options = base_options();
  options.io_depth = 4;
  options.dram_blocks = 1024;
  Trace trace;
  trace.config.num_blocks = 200;
  const uint64_t blocks_per_query = 50;
  for (uint64_t q = 0; q < 4; ++q) {
    TraceQuery query;
    query.query_id = q;
    TraceStream stream;
    stream.first_block_ns = 0;
    stream.block_interval_ns = 1;
    for (uint64_t i = 0; i < blocks_per_query; ++i) {
      stream.blocks.push_back(q * blocks_per_query + i);
    }
    query.streams.push_back(stream);
    trace.queries.push_back(query);
  }
  const SimMetrics metrics = run_simulation(trace, options);
  EXPECT_EQ(metrics.logical_block_requests, 200u);
  EXPECT_EQ(metrics.physical_block_reads, 200u);
  EXPECT_NEAR(coalescing_ratio(metrics), 0.0, 1e-9);
}

TEST(Des, IsDeterministic) {
  const SimOptions options = base_options();
  const SimMetrics first = run_simulation(tight_trace(4), options);
  const SimMetrics second = run_simulation(tight_trace(4), options);
  EXPECT_EQ(metrics_to_json(to_config(options), first),
            metrics_to_json(to_config(options), second));
}

TEST(Des, PoliciesShareSameReplayButMayDifferInOrder) {
  SimOptions fifo = base_options();
  fifo.policy = PolicyKind::FIFO;
  fifo.policy_name = "fifo";
  SimOptions sharing = base_options();
  sharing.policy = PolicyKind::SHARING_ONLY;
  sharing.policy_name = "sharing";
  const SimMetrics fifo_metrics = run_simulation(tight_trace(4), fifo);
  const SimMetrics sharing_metrics = run_simulation(tight_trace(4), sharing);
  EXPECT_EQ(fifo_metrics.logical_block_requests, sharing_metrics.logical_block_requests);
  EXPECT_LE(sharing_metrics.physical_block_reads, fifo_metrics.physical_block_reads);
}

}  // namespace
}  // namespace msaflow
