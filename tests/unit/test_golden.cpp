#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "des.hpp"
#include "msaflow/metrics.hpp"
#include "trace_reader.hpp"

#ifndef MSAFLOW_TEST_DATA_DIR
#error "MSAFLOW_TEST_DATA_DIR must be defined by the build"
#endif

namespace msaflow {
namespace {

TEST(Golden, TinyW1MsaflowV0MatchesCommittedMetrics) {
  const std::string dir = MSAFLOW_TEST_DATA_DIR;
  const Trace trace = read_trace(dir + "/tiny_w1.jsonl");

  SimOptions options;
  options.policy = PolicyKind::MSAFLOW_V0;
  options.policy_name = "msaflow-v0";
  options.block_bytes = 1000;
  options.bandwidth_bytes_per_ns = 1.0;
  options.base_latency_ns = 0;
  options.io_depth = 4;
  options.dram_blocks = 8;
  options.prefetch = false;
  options.starvation_threshold_ns = 1000000000ULL;

  const SimMetrics metrics = run_simulation(trace, options);
  const std::string actual = metrics_to_json(config_from_options(options), metrics) + "\n";

  std::ifstream input(dir + "/tiny_w1_msaflow.json");
  ASSERT_TRUE(input.good());
  std::stringstream expected;
  expected << input.rdbuf();
  EXPECT_EQ(actual, expected.str());
}

}  // namespace
}  // namespace msaflow
