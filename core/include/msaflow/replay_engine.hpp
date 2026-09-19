#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "msaflow/buffer.hpp"
#include "msaflow/cache.hpp"
#include "msaflow/io_backend.hpp"
#include "msaflow/metrics.hpp"
#include "msaflow/priority_policy.hpp"

namespace msaflow {

// Transport-agnostic replay engine shared by the discrete-event simulator and
// the (future) local storage runtime. It owns the scheduling/cache/inflight
// decision state and drives any StorageBackend. Time is injected via now_fn so
// the simulator can use a virtual clock while a real runtime uses a wall clock.
struct ReplayOptions {
  PolicyKind policy = PolicyKind::MSAFLOW_V0;
  std::string policy_name = "msaflow-v0";
  uint64_t dram_blocks = 1024;
  uint64_t block_bytes = 1024;
  double bandwidth_bytes_per_ns = 1.0;
  uint64_t base_latency_ns = 0;
  uint64_t io_depth = 1;
  bool prefetch = false;
  bool coalesce_inflight = true;
  uint64_t starvation_threshold_ns = 1000000000ULL;
  uint64_t scheduler_decision_cost_ns = 100;
  bool own_buffers = false;  // allocate a block-sized Buffer per physical block
  PolicyParams policy_params{};
  CacheConfig cache{};
};

class ReplayEngine {
 public:
  ReplayEngine(StorageBackend& backend, ReplayOptions options,
               std::function<uint64_t()> now_fn)
      : backend_(backend),
        options_(std::move(options)),
        now_(std::move(now_fn)),
        cache_(cache_config_from(options_)) {
    if (options_.io_depth < 1) {
      options_.io_depth = 1;
    }
  }

  void register_query(uint64_t query_id) {
    if (query_latency_.try_emplace(query_id, 0).second) {
      query_order_.push_back(query_id);
    }
  }

  // next_block == kNoNextBlock disables prefetch for this request.
  void request(uint64_t block_id, uint64_t next_block, uint64_t query_id) {
    const uint64_t now = now_();
    if (first_request_ns_ == kNoNextBlock) {
      first_request_ns_ = now;
    }
    request_block(block_id, query_id, now);
    if (options_.prefetch && next_block != kNoNextBlock) {
      maybe_prefetch(next_block, now);
    }
  }

  void handle_completion(uint64_t block_id, uint64_t now) {
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
      metrics_.storage_wait_ns_total += wait;
      metrics_.storage_wait_events += 1;
    }

    const CacheClass cache_class =
        (block.consumers() > 1 || block.prefetch) ? CacheClass::SHARED : CacheClass::STREAMING;
    cache_.admit(key(block_id), cache_class, now);

    const CacheConfig& config = cache_.config();
    if (static_cast<double>(cache_.occupancy()) >=
        config.high_watermark * config.capacity_blocks) {
      const uint64_t before = cache_.stats().evictions;
      cache_.evict_to_low_watermark(now);
      metrics_.evictions += cache_.stats().evictions - before;
    }
    if (cache_.occupancy() > metrics_.dram_occupancy_peak) {
      metrics_.dram_occupancy_peak = cache_.occupancy();
    }
    prune_buffers();
    if (now > last_completion_ns_) {
      last_completion_ns_ = now;
    }
    try_submit(now);
  }

  void try_submit(uint64_t now) {
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
        const double score = priority_score(options_.policy, input, options_.policy_params);
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
                             handle_completion(block_id, now_());
                           });
    }
  }

  bool has_outstanding() const {
    return !pending_.empty() || !inflight_.empty() || duplicates_outstanding_ > 0;
  }

  uint64_t service_ns() const {
    return options_.base_latency_ns +
           static_cast<uint64_t>(static_cast<double>(options_.block_bytes) /
                                 options_.bandwidth_bytes_per_ns);
  }

  const SimMetrics& metrics() const { return metrics_; }

  SimMetrics finish() {
    metrics_.queries = query_order_.size();
    metrics_.scheduler_decisions = decisions_;
    metrics_.scheduler_decision_ns_total = decisions_ * options_.scheduler_decision_cost_ns;
    metrics_.prefetch_waste = metrics_.prefetch_issued > metrics_.prefetch_hits
                                  ? metrics_.prefetch_issued - metrics_.prefetch_hits
                                  : 0;
    if (last_completion_ns_ > first_request_ns_) {
      metrics_.makespan_ns = last_completion_ns_ - first_request_ns_;
    }
    metrics_.query_latency_ns.reserve(query_order_.size());
    for (uint64_t query_id : query_order_) {
      metrics_.query_latency_ns.push_back(query_latency_[query_id]);
    }
    return metrics_;
  }

 private:
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

  static CacheConfig cache_config_from(const ReplayOptions& options) {
    CacheConfig config = options.cache;
    if (config.capacity_blocks == 0) {
      config.capacity_blocks = options.dram_blocks;
    }
    return config;
  }

  static constexpr uint64_t kNoNextBlock = static_cast<uint64_t>(-1);

  CacheKey key(uint64_t block_id) const { return CacheKey{{0}, {1, 0}, block_id}; }

  Buffer* buffer_for(uint64_t block_id) {
    if (!options_.own_buffers) {
      return scratch_buffer_.get();
    }
    const auto it = block_buffers_.find(block_id);
    if (it != block_buffers_.end()) {
      return it->second.get();
    }
    auto buffer = std::make_unique<Buffer>(options_.block_bytes);
    Buffer* raw = buffer.get();
    block_buffers_.emplace(block_id, std::move(buffer));
    return raw;
  }

  void prune_buffers() {
    if (!options_.own_buffers) {
      return;
    }
    for (auto it = block_buffers_.begin(); it != block_buffers_.end();) {
      if (!cache_.contains(key(it->first)) && inflight_.count(it->first) == 0 &&
          pending_.count(it->first) == 0) {
        it = block_buffers_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void request_block(uint64_t block_id, uint64_t query_id, uint64_t now) {
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
      if (options_.coalesce_inflight) {
        in_flight->second.waiters.push_back(Waiter{query_id, now});
        metrics_.coalesced_requests += 1;
        if (prefetched_.erase(block_id) > 0) {
          metrics_.prefetch_hits += 1;
        }
        return;
      }
      metrics_.physical_block_reads += 1;
      duplicates_outstanding_ += 1;
      backend_.submit_read(block_id, buffer_for(block_id),
                           [this, query_id, now](uint64_t, uint64_t, int) {
                             duplicates_outstanding_ -= 1;
                             uint64_t& worst = query_latency_[query_id];
                             const uint64_t wait = now_() - now;
                             if (wait > worst) {
                               worst = wait;
                             }
                             metrics_.storage_wait_ns_total += wait;
                             metrics_.storage_wait_events += 1;
                           });
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
  }

  void maybe_prefetch(uint64_t block_id, uint64_t now) {
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

  StorageBackend& backend_;
  ReplayOptions options_;
  std::function<uint64_t()> now_;
  CacheManager cache_;
  std::unique_ptr<Buffer> scratch_buffer_ = std::make_unique<Buffer>(4096);
  std::unordered_map<uint64_t, std::unique_ptr<Buffer>> block_buffers_;
  std::map<uint64_t, PendingBlock> pending_;
  std::unordered_map<uint64_t, PendingBlock> inflight_;
  std::map<uint64_t, bool> prefetched_;
  std::unordered_map<uint64_t, uint64_t> query_latency_;
  std::vector<uint64_t> query_order_;
  SimMetrics metrics_;
  uint64_t decisions_ = 0;
  std::size_t duplicates_outstanding_ = 0;
  uint64_t last_completion_ns_ = 0;
  uint64_t first_request_ns_ = kNoNextBlock;
};

}  // namespace msaflow
