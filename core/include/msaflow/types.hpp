#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace msaflow {

class Buffer;

struct DatabaseId {
  uint64_t id = 0;

  bool operator==(const DatabaseId& other) const = default;
};

struct DatabaseVersion {
  uint64_t major = 0;
  uint64_t minor = 0;

  bool operator==(const DatabaseVersion& other) const = default;
};

struct DatabaseMeta {
  DatabaseId id{};
  DatabaseVersion version{};
  uint64_t sequence_count = 0;
  uint64_t block_count = 0;
  uint64_t block_size = 0;
  uint64_t total_bytes = 0;
  std::string checksum;
};

struct CacheKey {
  DatabaseId db_id{};
  DatabaseVersion db_version{};
  uint64_t block_id = 0;

  bool operator==(const CacheKey& other) const = default;
};

struct CacheKeyHash {
  std::size_t operator()(const CacheKey& key) const noexcept {
    std::size_t h = std::hash<uint64_t>{}(key.db_id.id);
    h = _combine(h, std::hash<uint64_t>{}(key.db_version.major));
    h = _combine(h, std::hash<uint64_t>{}(key.db_version.minor));
    return _combine(h, std::hash<uint64_t>{}(key.block_id));
  }

 private:
  static std::size_t _combine(std::size_t seed, std::size_t value) noexcept {
    return value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
  }
};

struct BlockMeta {
  uint64_t block_id = 0;
  uint64_t file_offset = 0;
  uint32_t byte_size = 0;
  uint64_t first_seq_id = 0;
  uint64_t last_seq_id = 0;
};

enum class QueryStatus {
  CREATED,
  RUNNABLE,
  WAITING_BLOCK,
  PROCESSING,
  DONE,
  CANCELLED,
  ERROR,
};

struct QueryState {
  uint64_t query_id = 0;
  DatabaseId db_id{};
  DatabaseVersion db_version{};
  QueryStatus status = QueryStatus::CREATED;
  uint64_t current_block = 0;
  uint64_t next_block = 0;
  uint64_t blocks_processed = 0;
  uint64_t created_at_ns = 0;
  uint64_t last_progress_ns = 0;
  uint64_t deadline_ns = 0;
};

enum class BlockState {
  ABSENT,
  INFLIGHT,
  READY,
  IN_USE,
};

enum class CacheClass {
  NONE,
  SHARED,
  STREAMING,
};

struct BlockRuntime {
  uint64_t block_id = 0;
  BlockState state = BlockState::ABSENT;
  CacheClass cache_class = CacheClass::NONE;
  Buffer* buffer = nullptr;
  uint32_t active_consumers = 0;
  uint32_t future_consumers = 0;
  uint64_t last_access_ns = 0;
  uint64_t first_request_ns = 0;
  uint64_t io_request_id = 0;
};

struct InflightRequest {
  uint64_t io_request_id = 0;
  uint64_t block_id = 0;
  Buffer* buffer = nullptr;
  std::vector<uint64_t> consumers;
  uint64_t submit_ns = 0;
  uint64_t complete_ns = 0;
  int error_code = 0;
};

}  // namespace msaflow
