#include "des.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "msaflow/buffer.hpp"
#include "msaflow/cache.hpp"
#include "simulated_backend.hpp"

namespace msaflow {
namespace {

constexpr uint64_t kNoNextBlock = static_cast<uint64_t>(-1);

struct Waiter {
  uint64_t query_id = 0;
  uint64_t request_ns = 0;
};

struct PendingBlock {
  uint64_t first_request_ns = 0;
  bool prefetch = false;
  std::vector<Waiter> waiters;

  uint32_t consumers() const { return static_cast<uint32_t>(waiters.size()); }
};

struct Event {
  uint64_t time_ns = 0;
  uint64_t query_id = 0;
  uint64_t block_id = 0;
  uint64_t next_block = kNoNextBlock;
};

class Simulation {
 public:
  Simulation(const Trace& trace, const SimOptions& options)
      : trace_(trace),
        options_(options),
        backend_(SimulatedBackendConfig{options.block_bytes, options.bandwidth_bytes_per_ns,
                                        options.base_latency_ns,
                                        options.io_depth < 1 ? 1 : options.io_depth}),
        cache_(CacheConfig{options.dram_blocks, 0.9, 0.75}) {
    if (options_.io_depth < 1) {
      options_.io_depth = 1;
    }
  }

  SimMetrics run();

 private:
  CacheKey key(uint64_t block_id) const { return CacheKey{{0}, {1, 0}, block_id}; }

  uint64_t service_ns() const {
    return options_.base_latency_ns +
           static_cast<uint64_t>(static_cast<double>(options_.block_bytes) /
                                 options_.bandwidth_bytes_per_ns);
  }

  Buffer* buffer_for(uint64_t) { return scratch_buffer_.get(); }

  void request(uint64_t block_id, uint64_t next_block, uint64_t query_id, uint64_t now);
  void maybe_prefetch(uint64_t block_id, uint64_t now);
  void try_submit(uint64_t now);
  void handle_completion(uint64_t block_id, uint64_t now);

  Trace trace_;
  SimOptions options_;
  SimulatedBackend backend_;
  CacheManager cache_;
  std::unique_ptr<Buffer> scratch_buffer_ = std::make_unique<Buffer>(4096);
  std::map<uint64_t, PendingBlock> pending_;
  std::unordered_map<uint64_t, PendingBlock> inflight_;
  std::map<uint64_t, bool> prefetched_;
  std::unordered_map<uint64_t, uint64_t> query_latency_;
  SimMetrics metrics_;
  uint64_t decisions_ = 0;
  uint64_t last_completion_ns_ = 0;
  uint64_t first_arrival_ns_ = kNoNextBlock;
};

void Simulation::request(uint64_t block_id, uint64_t next_block, uint64_t query_id, uint64_t now) {
  metrics_.logical_block_requests += 1;
  const CacheKey cache_key = key(block_id);

  if (CacheEntry* hit = cache_.lookup(cache_key)) {
    cache_.touch(cache_key, now);
    if (hit->cache_class == CacheClass::SHARED) {
      metrics_.shared_cache_hits += 1;
    } else {
      metrics_.streaming_cache_hits += 1;
    }
    if (prefetched_.erase(block_id) > 0) {
      metrics_.prefetch_hits += 1;
    }
    if (hit->cache_class == CacheClass::STREAMING) {
      cache_.acquire(cache_key);
      cache_.release(cache_key, now);
    }
    return;
  }

  const auto in_flight = inflight_.find(block_id);
  if (in_flight != inflight_.end()) {
    in_flight->second.waiters.push_back(Waiter{query_id, now});
    metrics_.coalesced_requests += 1;
    if (prefetched_.erase(block_id) > 0) {
      metrics_.prefetch_hits += 1;
    }
    return;
  }

  const auto pending = pending_.find(block_id);
  if (pending != pending_.end()) {
    pending->second.waiters.push_back(Waiter{query_id, now});
    return;
  }

  PendingBlock block;
  block.first_request_ns = now;
  block.waiters.push_back(Waiter{query_id, now});
  pending_.emplace(block_id, std::move(block));
  try_submit(now);

  if (options_.prefetch && next_block != kNoNextBlock) {
    maybe_prefetch(next_block, now);
  }
}

void Simulation::maybe_prefetch(uint64_t block_id, uint64_t now) {
  if (cache_.lookup(key(block_id)) != nullptr) {
    return;
  }
  if (inflight_.count(block_id) > 0 || pending_.count(block_id) > 0) {
    return;
  }
  if (cache_.occupancy() >= options_.dram_blocks) {
    return;
  }
  PendingBlock block;
  block.first_request_ns = now;
  block.prefetch = true;
  pending_.emplace(block_id, std::move(block));
  prefetched_[block_id] = true;
  metrics_.prefetch_issued += 1;
  try_submit(now);
}

void Simulation::try_submit(uint64_t now) {
  while (inflight_.size() < options_.io_depth && !pending_.empty()) {
    auto best = pending_.begin();
    double best_score = 0.0;
    bool have_best = false;
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
      PriorityInput input;
      input.active_consumers = it->second.consumers();
      input.wait_ns = now - it->second.first_request_ns;
      input.io_cost_ns = service_ns();
      input.arrival_seq = it->second.first_request_ns;
      input.last_access_ns = it->second.first_request_ns;
      const double score = priority_score(options_.policy, input);
      if (!have_best || score > best_score ||
          (score == best_score && it->first < best->first)) {
        best = it;
        best_score = score;
        have_best = true;
      }
    }
    decisions_ += 1;
    const uint64_t block_id = best->first;
    PendingBlock block = std::move(best->second);
    pending_.erase(best);
    inflight_.emplace(block_id, std::move(block));
    metrics_.physical_block_reads += 1;
    backend_.submit_read(block_id, buffer_for(block_id),
                         [this, block_id](uint64_t, uint64_t, int) {
                           handle_completion(block_id, backend_.now_ns());
                         });
  }
}

void Simulation::handle_completion(uint64_t block_id, uint64_t now) {
  const auto it = inflight_.find(block_id);
  if (it == inflight_.end()) {
    return;
  }
  PendingBlock block = std::move(it->second);
  inflight_.erase(it);

  if (now - block.first_request_ns > options_.starvation_threshold_ns) {
    metrics_.starvation_count += 1;
  }
  for (const Waiter& waiter : block.waiters) {
    uint64_t& worst = query_latency_[waiter.query_id];
    const uint64_t wait = now - waiter.request_ns;
    if (wait > worst) {
      worst = wait;
    }
  }

  const CacheClass cache_class =
      (block.consumers() > 1 || block.prefetch) ? CacheClass::SHARED : CacheClass::STREAMING;
  cache_.admit(key(block_id), cache_class, now);

  const CacheConfig& config = cache_.config();
  if (static_cast<double>(cache_.occupancy()) >= config.high_watermark * config.capacity_blocks) {
    const uint64_t before = cache_.stats().evictions;
    cache_.evict_to_low_watermark(now);
    metrics_.evictions += cache_.stats().evictions - before;
  }
  if (cache_.occupancy() > metrics_.dram_occupancy_peak) {
    metrics_.dram_occupancy_peak = cache_.occupancy();
  }
  if (now > last_completion_ns_) {
    last_completion_ns_ = now;
  }
  try_submit(now);
}

SimMetrics Simulation::run() {
  std::vector<Event> events;
  for (const TraceQuery& query : trace_.queries) {
    for (const TraceStream& stream : query.streams) {
      for (std::size_t i = 0; i < stream.blocks.size(); ++i) {
        Event event;
        event.time_ns = stream.first_block_ns + i * stream.block_interval_ns;
        event.query_id = query.query_id;
        event.block_id = stream.blocks[i];
        event.next_block = (i + 1 < stream.blocks.size()) ? stream.blocks[i + 1] : kNoNextBlock;
        events.push_back(event);
      }
    }
    query_latency_[query.query_id] = 0;
  }
  std::sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
    if (left.time_ns != right.time_ns) {
      return left.time_ns < right.time_ns;
    }
    if (left.query_id != right.query_id) {
      return left.query_id < right.query_id;
    }
    return left.block_id < right.block_id;
  });

  if (!events.empty()) {
    first_arrival_ns_ = events.front().time_ns;
  }
  for (const Event& event : events) {
    backend_.advance_to(event.time_ns);
    request(event.block_id, event.next_block, event.query_id, event.time_ns);
  }

  const uint64_t last_event_ns = events.empty() ? 0 : events.back().time_ns;
  backend_.advance_to(last_event_ns);
  while (!pending_.empty() || !inflight_.empty()) {
    try_submit(backend_.now_ns());
    backend_.advance_to(backend_.now_ns() + service_ns() + 1);
  }

  metrics_.queries = trace_.queries.size();
  metrics_.scheduler_decisions = decisions_;
  metrics_.scheduler_decision_ns_total = decisions_ * options_.scheduler_decision_cost_ns;
  metrics_.prefetch_waste = metrics_.prefetch_issued > metrics_.prefetch_hits
                                ? metrics_.prefetch_issued - metrics_.prefetch_hits
                                : 0;
  if (last_completion_ns_ > first_arrival_ns_) {
    metrics_.makespan_ns = last_completion_ns_ - first_arrival_ns_;
  }
  metrics_.query_latency_ns.reserve(trace_.queries.size());
  for (const TraceQuery& query : trace_.queries) {
    metrics_.query_latency_ns.push_back(query_latency_[query.query_id]);
  }
  return metrics_;
}

}  // namespace

SimMetrics run_simulation(const Trace& trace, const SimOptions& options) {
  Simulation simulation(trace, options);
  return simulation.run();
}

}  // namespace msaflow
