#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace msaflow {

struct TraceConfig {
  std::string db;
  uint64_t num_blocks = 0;
  uint64_t num_shards = 0;
  uint64_t block_bytes = 0;
};

struct TraceStream {
  uint64_t shard_id = 0;
  uint64_t first_block_ns = 0;
  uint64_t block_interval_ns = 0;
  std::vector<uint64_t> blocks;
};

struct TraceQuery {
  uint64_t query_id = 0;
  uint64_t arrival_ns = 0;
  std::vector<TraceStream> streams;
};

struct Trace {
  TraceConfig config;
  std::vector<TraceQuery> queries;
};

Trace read_trace(const std::string& path, uint64_t default_interval_ns = 1000000);

}  // namespace msaflow
