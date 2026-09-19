#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "trace_reader.hpp"

namespace msaflow {
namespace {

std::string write_temp(const std::string& contents) {
  static int counter = 0;
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / ("msaflow_trace_" + std::to_string(counter++) + ".jsonl");
  std::ofstream out(path);
  out << contents;
  out.close();
  return path.string();
}

TEST(TraceReader, ParsesCanonicalTrace) {
  const std::string path = write_temp(
      "{\"type\":\"config\",\"format_version\":1,\"db\":\"uniref90\",\"num_blocks\":64,"
      "\"num_shards\":4,\"block_bytes\":1024}\n"
      "{\"type\":\"query\",\"query_id\":1,\"arrival_ns\":5,\"streams\":["
      "{\"shard_id\":0,\"first_block_ns\":5,\"block_interval_ns\":10,\"blocks\":[0,1,2]},"
      "{\"shard_id\":1,\"first_block_ns\":5,\"block_interval_ns\":20,\"blocks\":[3]}]}\n");
  const Trace trace = read_trace(path);
  std::remove(path.c_str());
  EXPECT_EQ(trace.config.db, "uniref90");
  EXPECT_EQ(trace.config.num_blocks, 64u);
  EXPECT_EQ(trace.config.num_shards, 4u);
  ASSERT_EQ(trace.queries.size(), 1u);
  const TraceQuery& query = trace.queries[0];
  EXPECT_EQ(query.query_id, 1u);
  EXPECT_EQ(query.arrival_ns, 5u);
  ASSERT_EQ(query.streams.size(), 2u);
  EXPECT_EQ(query.streams[0].shard_id, 0u);
  EXPECT_EQ(query.streams[0].block_interval_ns, 10u);
  EXPECT_EQ(query.streams[0].blocks, (std::vector<uint64_t>{0, 1, 2}));
  EXPECT_EQ(query.streams[1].blocks, (std::vector<uint64_t>{3}));
}

TEST(TraceReader, ParsesFlatSpecForm) {
  const std::string path = write_temp("{\"query_id\":1,\"db\":\"x\",\"blocks\":[4,5]}\n");
  const Trace trace = read_trace(path, 100);
  std::remove(path.c_str());
  ASSERT_EQ(trace.queries.size(), 1u);
  ASSERT_EQ(trace.queries[0].streams.size(), 1u);
  EXPECT_EQ(trace.queries[0].streams[0].block_interval_ns, 100u);
  EXPECT_EQ(trace.queries[0].streams[0].first_block_ns, 0u);
  EXPECT_EQ(trace.queries[0].streams[0].blocks, (std::vector<uint64_t>{4, 5}));
}

TEST(TraceReader, RejectsMalformedLine) {
  const std::string path = write_temp("{not json}\n");
  EXPECT_THROW(read_trace(path), std::runtime_error);
  std::remove(path.c_str());
}

}  // namespace
}  // namespace msaflow
