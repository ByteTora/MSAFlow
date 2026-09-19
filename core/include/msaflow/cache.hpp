#pragma once

#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "msaflow/types.hpp"

namespace msaflow {

struct CacheEntry {
  CacheKey key{};
  CacheClass cache_class = CacheClass::NONE;
  uint32_t active_consumers = 0;
  uint32_t future_consumers = 0;
  uint64_t last_access_ns = 0;
};

struct CacheConfig {
  uint64_t capacity_blocks = 0;
  double high_watermark = 0.90;
  double low_watermark = 0.75;
  double active_weight = 2.0;
  double future_weight = 3.0;
  double recency_weight = 1.0;
};

struct CacheStats {
  uint64_t admissions = 0;
  uint64_t hits = 0;
  uint64_t evictions = 0;
  uint64_t streaming_released = 0;
};

class CacheManager {
 public:
  explicit CacheManager(CacheConfig config) : config_(config) {}

  bool admit(const CacheKey& key, CacheClass cache_class, uint64_t now_ns) {
    if (entries_.find(key) != entries_.end()) {
      return false;
    }
    CacheEntry entry;
    entry.key = key;
    entry.cache_class = cache_class;
    entry.last_access_ns = now_ns;
    entries_.emplace(key, entry);
    stats_.admissions += 1;
    if (occupancy() > config_.capacity_blocks) {
      evict_to_low_watermark(now_ns);
    }
    return true;
  }

  CacheEntry* lookup(const CacheKey& key) {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
      return nullptr;
    }
    stats_.hits += 1;
    return &it->second;
  }

  bool touch(const CacheKey& key, uint64_t now_ns) {
    CacheEntry* entry = find(key);
    if (entry == nullptr) {
      return false;
    }
    entry->last_access_ns = now_ns;
    return true;
  }

  bool acquire(const CacheKey& key) {
    CacheEntry* entry = find(key);
    if (entry == nullptr) {
      return false;
    }
    entry->active_consumers += 1;
    return true;
  }

  bool release(const CacheKey& key, uint64_t now_ns) {
    CacheEntry* entry = find(key);
    if (entry == nullptr || entry->active_consumers == 0) {
      return false;
    }
    entry->active_consumers -= 1;
    entry->last_access_ns = now_ns;
    if (entry->active_consumers == 0 && entry->cache_class == CacheClass::STREAMING) {
      entries_.erase(key);
      stats_.streaming_released += 1;
    }
    return true;
  }

  bool set_future_consumers(const CacheKey& key, uint32_t future) {
    CacheEntry* entry = find(key);
    if (entry == nullptr) {
      return false;
    }
    entry->future_consumers = future;
    return true;
  }

  double keep_score(const CacheKey& key, uint64_t now_ns) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
      return 0.0;
    }
    return keep_score(it->second, now_ns);
  }

  double keep_score(const CacheEntry& entry, uint64_t now_ns) const {
    const double age_ns = static_cast<double>(now_ns - entry.last_access_ns);
    return config_.active_weight * std::log2(1.0 + entry.active_consumers) +
           config_.future_weight * std::log2(1.0 + entry.future_consumers) +
           config_.recency_weight / (1.0 + age_ns);
  }

  void evict_to_low_watermark(uint64_t now_ns) {
    const uint64_t target = static_cast<uint64_t>(config_.low_watermark * config_.capacity_blocks);
    while (occupancy() > target) {
      const CacheKey* victim = lowest_scoring_evictable(now_ns);
      if (victim == nullptr) {
        break;
      }
      entries_.erase(*victim);
      stats_.evictions += 1;
    }
  }

  std::size_t occupancy() const { return entries_.size(); }
  const CacheStats& stats() const { return stats_; }
  const CacheConfig& config() const { return config_; }

 private:
  CacheEntry* find(const CacheKey& key) {
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
  }

  const CacheKey* lowest_scoring_evictable(uint64_t now_ns) const {
    const CacheEntry* victim = nullptr;
    double victim_score = 0.0;
    for (const auto& [key, entry] : entries_) {
      if (entry.active_consumers > 0) {
        continue;
      }
      const double score = keep_score(entry, now_ns);
      if (victim == nullptr || score < victim_score) {
        victim = &entry;
        victim_score = score;
      }
    }
    return victim == nullptr ? nullptr : &victim->key;
  }

  CacheConfig config_;
  std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> entries_;
  CacheStats stats_;
};

}  // namespace msaflow
