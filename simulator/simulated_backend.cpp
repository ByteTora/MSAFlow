#include "simulated_backend.hpp"

#include <cstddef>

namespace msaflow {

SimulatedBackend::SimulatedBackend(SimulatedBackendConfig config) : config_(config) {}

uint64_t SimulatedBackend::submit_read(uint64_t block_id, Buffer* buffer, ReadCallback callback) {
  Request request;
  request.io_id = next_io_id_++;
  request.block_id = block_id;
  request.buffer = buffer;
  request.callback = std::move(callback);
  if (fail_next_err_ >= 0) {
    request.err = fail_next_err_;
    fail_next_err_ = -1;
  }
  if (in_flight_.size() < config_.queue_depth) {
    request.complete_ns = now_ns_ + service_ns();
    in_flight_.push_back(std::move(request));
  } else {
    waiting_.push_back(std::move(request));
  }
  return next_io_id_ - 1;
}

void SimulatedBackend::poll() {
  const uint64_t target_ns = now_ns_;
  while (true) {
    start_pending();
    const std::size_t index = earliest_completed(target_ns);
    if (index == in_flight_.size()) {
      break;
    }
    const Request done = in_flight_[index];
    in_flight_.erase(in_flight_.begin() + static_cast<std::ptrdiff_t>(index));
    now_ns_ = done.complete_ns;
    if (done.callback) {
      done.callback(done.io_id, done.block_id, done.err);
    }
  }
  now_ns_ = target_ns;
}

void SimulatedBackend::advance_to(uint64_t now_ns) {
  now_ns_ = now_ns;
  poll();
}

void SimulatedBackend::fail_next_read_with(int err) { fail_next_err_ = err; }

uint64_t SimulatedBackend::service_ns() const {
  const double transfer_ns =
      static_cast<double>(config_.block_bytes) / config_.bandwidth_bytes_per_ns;
  return config_.base_latency_ns + static_cast<uint64_t>(transfer_ns);
}

void SimulatedBackend::start_pending() {
  while (!waiting_.empty() && in_flight_.size() < config_.queue_depth) {
    Request request = waiting_.front();
    waiting_.pop_front();
    request.complete_ns = now_ns_ + service_ns();
    in_flight_.push_back(std::move(request));
  }
}

std::size_t SimulatedBackend::earliest_completed(uint64_t target_ns) const {
  std::size_t best = in_flight_.size();
  for (std::size_t i = 0; i < in_flight_.size(); ++i) {
    if (in_flight_[i].complete_ns > target_ns) {
      continue;
    }
    if (best == in_flight_.size() || in_flight_[i].complete_ns < in_flight_[best].complete_ns ||
        (in_flight_[i].complete_ns == in_flight_[best].complete_ns &&
         in_flight_[i].io_id < in_flight_[best].io_id)) {
      best = i;
    }
  }
  return best;
}

}  // namespace msaflow
