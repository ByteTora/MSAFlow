#pragma once

#include <cstdint>
#include <string>

#include "msaflow/metrics.hpp"
#include "msaflow/priority_policy.hpp"
#include "trace_reader.hpp"

namespace msaflow {

struct SimOptions {
  PolicyKind policy = PolicyKind::MSAFLOW_V0;
  std::string policy_name = "msaflow-v0";
  uint64_t dram_blocks = 1024;
  uint64_t block_bytes = 1024;
  double bandwidth_bytes_per_ns = 1.0;
  uint64_t base_latency_ns = 0;
  uint64_t io_depth = 1;
  bool prefetch = false;
  uint64_t starvation_threshold_ns = 1000000000ULL;
  uint64_t scheduler_decision_cost_ns = 100;
};

SimMetrics run_simulation(const Trace& trace, const SimOptions& options);

SimConfig config_from_options(const SimOptions& options);

}  // namespace msaflow
