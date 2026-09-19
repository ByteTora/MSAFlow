#include "trace_reader.hpp"

#include <fstream>
#include <stdexcept>

#include "json_lite.hpp"

namespace msaflow {
namespace {

using json_lite::Value;

uint64_t to_u64(const Value& value) { return static_cast<uint64_t>(value.num()); }

TraceStream parse_stream(const Value& stream) {
  TraceStream out;
  if (stream.has("shard_id")) {
    out.shard_id = to_u64(stream.at("shard_id"));
  }
  out.first_block_ns = to_u64(stream.at("first_block_ns"));
  out.block_interval_ns = to_u64(stream.at("block_interval_ns"));
  for (const Value& block : stream.at("blocks").items()) {
    out.blocks.push_back(to_u64(block));
  }
  return out;
}

TraceQuery parse_query(const Value& row, uint64_t default_interval_ns) {
  TraceQuery query;
  query.query_id = to_u64(row.at("query_id"));
  if (row.has("arrival_ns")) {
    query.arrival_ns = to_u64(row.at("arrival_ns"));
  }
  if (row.has("streams")) {
    for (const Value& stream : row.at("streams").items()) {
      query.streams.push_back(parse_stream(stream));
    }
    return query;
  }
  TraceStream stream;
  stream.first_block_ns = query.arrival_ns;
  stream.block_interval_ns = row.has("block_interval_ns")
                                 ? to_u64(row.at("block_interval_ns"))
                                 : default_interval_ns;
  for (const Value& block : row.at("blocks").items()) {
    stream.blocks.push_back(to_u64(block));
  }
  query.streams.push_back(std::move(stream));
  return query;
}

}  // namespace

Trace read_trace(const std::string& path, uint64_t default_interval_ns) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("trace_reader: cannot open " + path);
  }
  Trace trace;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const Value row = json_lite::parse(line);
    if (!row.is_object()) {
      throw std::runtime_error("trace_reader: line is not a JSON object");
    }
    if (row.has("type") && row.at("type").str() == "config") {
      trace.config.db = row.has("db") ? row.at("db").str() : "";
      trace.config.num_blocks = row.has("num_blocks") ? to_u64(row.at("num_blocks")) : 0;
      trace.config.num_shards = row.has("num_shards") ? to_u64(row.at("num_shards")) : 0;
      trace.config.block_bytes = row.has("block_bytes") ? to_u64(row.at("block_bytes")) : 0;
      continue;
    }
    if (row.has("query_id")) {
      trace.queries.push_back(parse_query(row, default_interval_ns));
    }
  }
  return trace;
}

}  // namespace msaflow
