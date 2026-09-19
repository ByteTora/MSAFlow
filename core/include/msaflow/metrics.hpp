#pragma once

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace msaflow {

struct SimConfig {
  std::string policy;
  uint64_t dram_blocks = 0;
  uint64_t block_bytes = 0;
  double bandwidth_bytes_per_ns = 1.0;
  uint64_t base_latency_ns = 0;
  uint64_t io_depth = 1;
  bool prefetch = false;
  uint64_t starvation_threshold_ns = 0;
};

struct SimMetrics {
  uint64_t queries = 0;
  uint64_t logical_block_requests = 0;
  uint64_t physical_block_reads = 0;
  uint64_t coalesced_requests = 0;
  uint64_t shared_cache_hits = 0;
  uint64_t streaming_cache_hits = 0;
  uint64_t prefetch_issued = 0;
  uint64_t prefetch_hits = 0;
  uint64_t prefetch_waste = 0;
  uint64_t evictions = 0;
  uint64_t dram_occupancy_peak = 0;
  uint64_t starvation_count = 0;
  uint64_t scheduler_decisions = 0;
  uint64_t scheduler_decision_ns_total = 0;
  uint64_t makespan_ns = 0;
  std::vector<uint64_t> query_latency_ns;
};

inline double coalescing_ratio(const SimMetrics& metrics) {
  if (metrics.logical_block_requests == 0) {
    return 0.0;
  }
  return 1.0 - static_cast<double>(metrics.physical_block_reads) /
                   static_cast<double>(metrics.logical_block_requests);
}

inline uint64_t percentile(std::vector<uint64_t> values, double q) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t index =
      std::min(values.size() - 1, static_cast<std::size_t>(q * (values.size() - 1)));
  return values[index];
}

inline double throughput_qps(const SimMetrics& metrics) {
  if (metrics.makespan_ns == 0) {
    return 0.0;
  }
  return static_cast<double>(metrics.queries) * 1e9 / static_cast<double>(metrics.makespan_ns);
}

inline std::string metrics_to_json(const SimConfig& config, const SimMetrics& metrics) {
  std::ostringstream out;
  out << "{";
  out << "\"config\":{";
  out << "\"policy\":\"" << config.policy << "\",";
  out << "\"dram_blocks\":" << config.dram_blocks << ",";
  out << "\"block_bytes\":" << config.block_bytes << ",";
  out << "\"io_depth\":" << config.io_depth << ",";
  out << "\"prefetch\":" << (config.prefetch ? "true" : "false");
  out << "},";
  out << "\"metrics\":{";
  out << "\"queries\":" << metrics.queries << ",";
  out << "\"logical_block_requests\":" << metrics.logical_block_requests << ",";
  out << "\"physical_block_reads\":" << metrics.physical_block_reads << ",";
  out << "\"coalesced_requests\":" << metrics.coalesced_requests << ",";
  out << "\"coalescing_ratio\":" << coalescing_ratio(metrics) << ",";
  out << "\"shared_cache_hits\":" << metrics.shared_cache_hits << ",";
  out << "\"streaming_cache_hits\":" << metrics.streaming_cache_hits << ",";
  out << "\"prefetch_issued\":" << metrics.prefetch_issued << ",";
  out << "\"prefetch_hits\":" << metrics.prefetch_hits << ",";
  out << "\"prefetch_waste\":" << metrics.prefetch_waste << ",";
  out << "\"evictions\":" << metrics.evictions << ",";
  out << "\"dram_occupancy_peak\":" << metrics.dram_occupancy_peak << ",";
  out << "\"starvation_count\":" << metrics.starvation_count << ",";
  out << "\"scheduler_decisions\":" << metrics.scheduler_decisions << ",";
  out << "\"scheduler_decision_ns_total\":" << metrics.scheduler_decision_ns_total << ",";
  out << "\"makespan_ns\":" << metrics.makespan_ns << ",";
  out << "\"latency_p50_ns\":" << percentile(metrics.query_latency_ns, 0.50) << ",";
  out << "\"latency_p95_ns\":" << percentile(metrics.query_latency_ns, 0.95) << ",";
  out << "\"latency_p99_ns\":" << percentile(metrics.query_latency_ns, 0.99) << ",";
  out << "\"throughput_qps\":" << throughput_qps(metrics);
  out << "}}";
  return out.str();
}

}  // namespace msaflow
