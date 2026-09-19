#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "msaflow/io_backend.hpp"

namespace msaflow {

struct SimulatedBackendConfig {
  uint64_t block_bytes = 0;
  double bandwidth_bytes_per_ns = 1.0;
  uint64_t base_latency_ns = 0;
  uint64_t queue_depth = 1;
};

class SimulatedBackend : public StorageBackend {
 public:
  explicit SimulatedBackend(SimulatedBackendConfig config);

  uint64_t submit_read(uint64_t block_id, Buffer* buffer, ReadCallback callback) override;
  void poll() override;

  void advance_to(uint64_t now_ns);
  uint64_t now_ns() const { return now_ns_; }
  void fail_next_read_with(int err);

 private:
  struct Request {
    uint64_t io_id = 0;
    uint64_t block_id = 0;
    Buffer* buffer = nullptr;
    ReadCallback callback;
    uint64_t complete_ns = 0;
    int err = 0;
  };

  uint64_t service_ns() const;
  void start_pending();
  std::size_t earliest_completed(uint64_t target_ns) const;

  SimulatedBackendConfig config_;
  uint64_t now_ns_ = 0;
  uint64_t next_io_id_ = 0;
  int fail_next_err_ = -1;
  std::deque<Request> waiting_;
  std::vector<Request> in_flight_;
};

}  // namespace msaflow
