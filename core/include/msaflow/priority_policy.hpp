#pragma once

#include <cmath>
#include <cstdint>

namespace msaflow {

enum class PolicyKind {
  FIFO,
  LRU,
  SHARING_ONLY,
  URGENCY_ONLY,
  MSAFLOW_V0,
};

struct PolicyParams {
  double urgency_weight = 1.0;
  double sharing_weight = 1e6;
  double io_cost_weight = 1.0;
};

struct PriorityInput {
  uint32_t active_consumers = 0;
  uint64_t wait_ns = 0;
  uint64_t io_cost_ns = 0;
  uint64_t arrival_seq = 0;
  uint64_t last_access_ns = 0;
};

inline double sharing_score(uint32_t consumers) {
  return std::log2(1.0 + static_cast<double>(consumers));
}

inline double priority_score(PolicyKind kind, const PriorityInput& in,
                             const PolicyParams& params = {}) {
  switch (kind) {
    case PolicyKind::FIFO:
      return -static_cast<double>(in.arrival_seq);
    case PolicyKind::LRU:
      return -static_cast<double>(in.last_access_ns);
    case PolicyKind::SHARING_ONLY:
      return sharing_score(in.active_consumers);
    case PolicyKind::URGENCY_ONLY:
      return static_cast<double>(in.wait_ns);
    case PolicyKind::MSAFLOW_V0:
      return params.urgency_weight * static_cast<double>(in.wait_ns) +
             params.sharing_weight * sharing_score(in.active_consumers) -
             params.io_cost_weight * static_cast<double>(in.io_cost_ns);
  }
  return 0.0;
}

}  // namespace msaflow
