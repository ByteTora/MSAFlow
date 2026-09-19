// MSAFlow local storage runtime (Phase 2A).
//
// Replays Trace Format v1 against a Block Database through the shared
// ReplayEngine. Two backends are available:
//   - sim   : deterministic virtual-time SimulatedBackend (decision parity with
//             the discrete-event simulator; used by the cross-check harness);
//   - pread : real file I/O via PreadBackend (buffered or O_DIRECT).
//
// Output JSON has the simulator's "config"/"metrics" objects plus a "storage"
// object with the spec §18.2 storage metrics.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "msaflow/block_db.hpp"
#include "msaflow/metrics.hpp"
#include "msaflow/replay_engine.hpp"
#include "simulated_backend.hpp"
#include "sync/pread_backend.hpp"
#include "trace_reader.hpp"

#ifdef MSAFLOW_HAVE_IO_URING
#include "local_nvme/io_uring_backend.hpp"
#endif

namespace {

using msaflow::PolicyKind;

constexpr uint64_t kNoNextBlock = static_cast<uint64_t>(-1);

struct Options {
  std::string trace_path;
  std::string db_dir;
  std::string backend = "sim";
  std::string policy_name = "msaflow-v0";
  PolicyKind policy = PolicyKind::MSAFLOW_V0;
  std::string out_path;
  uint64_t dram_blocks = 1024;
  uint64_t block_bytes = 0;
  double bandwidth_bytes_per_ns = 1.0;
  uint64_t io_depth = 1;
  bool prefetch = false;
  bool coalesce_inflight = true;
  uint64_t starvation_threshold_ns = 60000000000ULL;
  bool direct_io = false;
  bool pace = false;
};

struct Event {
  uint64_t time_ns = 0;
  uint64_t query_id = 0;
  uint64_t block_id = 0;
  uint64_t next_block = kNoNextBlock;
};

bool flag_value(int argc, char** argv, const std::string& name, std::string& out) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == name) {
      out = argv[i + 1];
      return true;
    }
  }
  return false;
}

bool has_flag(int argc, char** argv, const std::string& name) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == name) {
      return true;
    }
  }
  return false;
}

bool parse_policy(const std::string& name, PolicyKind& kind) {
  if (name == "fifo") {
    kind = PolicyKind::FIFO;
  } else if (name == "lru") {
    kind = PolicyKind::LRU;
  } else if (name == "sharing") {
    kind = PolicyKind::SHARING_ONLY;
  } else if (name == "urgency") {
    kind = PolicyKind::URGENCY_ONLY;
  } else if (name == "msaflow-v0") {
    kind = PolicyKind::MSAFLOW_V0;
  } else {
    return false;
  }
  return true;
}

void usage() {
  std::cerr << "usage: msaflow-runtime --trace FILE [--db DIR] "
               "[--backend sim|pread] "
               "[--policy {fifo,lru,sharing,urgency,msaflow-v0}] "
               "[--dram-blocks N] [--block-bytes B] [--bandwidth-gbps X] "
               "[--io-depth D] [--prefetch on|off] [--no-coalesce] [--direct] "
               "[--pace] [--starvation-threshold-ms MS] [--out FILE]\n";
}

std::vector<Event> build_events(const msaflow::Trace& trace) {
  std::vector<Event> events;
  for (const msaflow::TraceQuery& query : trace.queries) {
    for (const msaflow::TraceStream& stream : query.streams) {
      for (std::size_t i = 0; i < stream.blocks.size(); ++i) {
        Event event;
        event.time_ns = stream.first_block_ns + i * stream.block_interval_ns;
        event.query_id = query.query_id;
        event.block_id = stream.blocks[i];
        event.next_block =
            (i + 1 < stream.blocks.size()) ? stream.blocks[i + 1] : kNoNextBlock;
        events.push_back(event);
      }
    }
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
  return events;
}

msaflow::ReplayOptions replay_options_from(const Options& options) {
  msaflow::ReplayOptions replay;
  replay.policy = options.policy;
  replay.policy_name = options.policy_name;
  replay.dram_blocks = options.dram_blocks;
  replay.block_bytes = options.block_bytes;
  replay.bandwidth_bytes_per_ns = options.bandwidth_bytes_per_ns;
  replay.io_depth = options.io_depth;
  replay.prefetch = options.prefetch;
  replay.coalesce_inflight = options.coalesce_inflight;
  replay.starvation_threshold_ns = options.starvation_threshold_ns;
  return replay;
}

msaflow::SimConfig sim_config_from(const Options& options) {
  msaflow::SimConfig config;
  config.policy = options.policy_name;
  config.dram_blocks = options.dram_blocks;
  config.block_bytes = options.block_bytes;
  config.bandwidth_bytes_per_ns = options.bandwidth_bytes_per_ns;
  config.io_depth = options.io_depth;
  config.prefetch = options.prefetch;
  config.coalesce_inflight = options.coalesce_inflight;
  config.starvation_threshold_ns = options.starvation_threshold_ns;
  return config;
}

std::string storage_to_json(const std::string& backend, uint64_t physical_bytes,
                            uint64_t logical_bytes, uint64_t physical_reads,
                            uint64_t makespan_ns, uint64_t wait_ns,
                            uint64_t wait_events, uint64_t read_errors) {
  const double makespan_s = static_cast<double>(makespan_ns) / 1e9;
  const double iops = makespan_s > 0.0 ? static_cast<double>(physical_reads) / makespan_s : 0.0;
  const double bandwidth = makespan_s > 0.0 ? static_cast<double>(physical_bytes) / makespan_s : 0.0;
  const uint64_t avg_latency = wait_events > 0 ? wait_ns / wait_events : 0;
  std::ostringstream out;
  out << "{";
  out << "\"backend\":\"" << backend << "\",";
  out << "\"physical_bytes_read\":" << physical_bytes << ",";
  out << "\"logical_bytes_consumed\":" << logical_bytes << ",";
  out << "\"iops\":" << iops << ",";
  out << "\"read_bandwidth_bytes_per_s\":" << bandwidth << ",";
  out << "\"avg_read_latency_ns\":" << avg_latency << ",";
  out << "\"storage_wait_ns_total\":" << wait_ns << ",";
  out << "\"storage_wait_events\":" << wait_events << ",";
  out << "\"read_errors\":" << read_errors;
  out << "}";
  return out.str();
}

uint64_t elapsed_ns(std::chrono::steady_clock::time_point start) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

// Drives real (non-virtual-time) backends: pread and io_uring both expose
// pending_count()/wait_one() so the loop is shared.
template <typename Backend>
void replay_realtime(Backend& backend, msaflow::ReplayEngine& engine,
                     const std::vector<Event>& events, uint64_t first_time_ns,
                     const Options& options, std::chrono::steady_clock::time_point start) {
  // Issue every request sharing a timestamp before draining, so concurrent
  // queries can coalesce on the same in-flight block.
  std::size_t index = 0;
  while (index < events.size()) {
    const uint64_t timestamp = events[index].time_ns;
    if (options.pace) {
      const uint64_t target = timestamp - first_time_ns;
      const uint64_t now = elapsed_ns(start);
      if (target > now) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(target - now));
      }
    }
    while (index < events.size() && events[index].time_ns == timestamp) {
      engine.request(events[index].block_id, events[index].next_block, events[index].query_id);
      ++index;
    }
    while (backend.pending_count() > 0) {
      backend.wait_one();
    }
  }
  while (engine.has_outstanding()) {
    engine.try_submit(elapsed_ns(start));
    while (backend.pending_count() > 0) {
      backend.wait_one();
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  std::string value;
  if (!flag_value(argc, argv, "--trace", options.trace_path) || argc < 3) {
    usage();
    return 2;
  }
  flag_value(argc, argv, "--db", options.db_dir);
  flag_value(argc, argv, "--backend", options.backend);
  flag_value(argc, argv, "--policy", options.policy_name);
  if (!parse_policy(options.policy_name, options.policy)) {
    std::cerr << "unknown policy: " << options.policy_name << "\n";
    return 2;
  }
  if (flag_value(argc, argv, "--dram-blocks", value)) {
    options.dram_blocks = std::strtoull(value.c_str(), nullptr, 10);
  }
  if (flag_value(argc, argv, "--block-bytes", value)) {
    options.block_bytes = std::strtoull(value.c_str(), nullptr, 10);
  }
  if (flag_value(argc, argv, "--bandwidth-gbps", value)) {
    options.bandwidth_bytes_per_ns = std::strtod(value.c_str(), nullptr);
  }
  if (flag_value(argc, argv, "--io-depth", value)) {
    options.io_depth = std::strtoull(value.c_str(), nullptr, 10);
  }
  if (flag_value(argc, argv, "--starvation-threshold-ms", value)) {
    options.starvation_threshold_ns =
        static_cast<uint64_t>(std::strtod(value.c_str(), nullptr) * 1e6);
  }
  if (flag_value(argc, argv, "--prefetch", value)) {
    options.prefetch = (value == "on" || value == "true" || value == "1");
  }
  options.coalesce_inflight = !has_flag(argc, argv, "--no-coalesce");
  options.direct_io = has_flag(argc, argv, "--direct");
  options.pace = has_flag(argc, argv, "--pace");
  flag_value(argc, argv, "--out", options.out_path);

  if (options.backend != "sim" && options.backend != "pread" &&
      options.backend != "io_uring") {
    std::cerr << "unknown backend: " << options.backend << "\n";
    return 2;
  }
#ifndef MSAFLOW_HAVE_IO_URING
  if (options.backend == "io_uring") {
    std::cerr << "io_uring backend not compiled in (liburing not found at build time)\n";
    return 2;
  }
#endif

  const msaflow::Trace trace = msaflow::read_trace(options.trace_path);
  if (options.block_bytes == 0) {
    options.block_bytes = trace.config.block_bytes > 0 ? trace.config.block_bytes : 1024;
  }

  std::unique_ptr<msaflow::BlockDb> block_db;
  if (options.backend != "sim") {
    if (options.db_dir.empty()) {
      std::cerr << "--db DIR is required for the " << options.backend << " backend\n";
      return 2;
    }
    block_db = std::make_unique<msaflow::BlockDb>(msaflow::BlockDb::open(options.db_dir));
    options.block_bytes = std::max(options.block_bytes, block_db->max_block_bytes());
  }

  const std::vector<Event> events = build_events(trace);
  msaflow::SimMetrics metrics;
  uint64_t physical_bytes = 0;
  uint64_t read_errors = 0;

  if (options.backend == "sim") {
    msaflow::SimulatedBackend backend(msaflow::SimulatedBackendConfig{
        options.block_bytes, options.bandwidth_bytes_per_ns, 0, options.io_depth});
    msaflow::ReplayEngine engine(backend, replay_options_from(options),
                                 [&backend]() { return backend.now_ns(); });
    for (const msaflow::TraceQuery& query : trace.queries) {
      engine.register_query(query.query_id);
    }
    for (const Event& event : events) {
      backend.advance_to(event.time_ns);
      engine.request(event.block_id, event.next_block, event.query_id);
    }
    const uint64_t last_event_ns = events.empty() ? 0 : events.back().time_ns;
    backend.advance_to(last_event_ns);
    while (engine.has_outstanding()) {
      engine.try_submit(backend.now_ns());
      backend.advance_to(backend.now_ns() + engine.service_ns() + 1);
    }
    metrics = engine.finish();
    physical_bytes = metrics.physical_block_reads * options.block_bytes;
  } else if (options.backend == "pread") {
    msaflow::ReplayOptions replay = replay_options_from(options);
    replay.own_buffers = true;
    msaflow::PreadBackend::Options pread_options;
    pread_options.direct_io = options.direct_io;
    msaflow::PreadBackend backend(block_db->data_path(), *block_db, pread_options);
    const auto start = std::chrono::steady_clock::now();
    msaflow::ReplayEngine engine(backend, replay,
                                 [&start]() { return elapsed_ns(start); });
    for (const msaflow::TraceQuery& query : trace.queries) {
      engine.register_query(query.query_id);
    }
    const uint64_t first_time_ns = events.empty() ? 0 : events.front().time_ns;
    replay_realtime(backend, engine, events, first_time_ns, options, start);
    metrics = engine.finish();
    physical_bytes = backend.physical_bytes_read();
    read_errors = backend.read_errors();
  } else {
#ifdef MSAFLOW_HAVE_IO_URING
    msaflow::ReplayOptions replay = replay_options_from(options);
    replay.own_buffers = true;
    msaflow::IoUringBackend::Options uring_options(
        static_cast<unsigned>(options.io_depth), options.direct_io, 4096);
    msaflow::IoUringBackend backend(block_db->data_path(), *block_db, uring_options);
    const auto start = std::chrono::steady_clock::now();
    msaflow::ReplayEngine engine(backend, replay,
                                 [&start]() { return elapsed_ns(start); });
    for (const msaflow::TraceQuery& query : trace.queries) {
      engine.register_query(query.query_id);
    }
    const uint64_t first_time_ns = events.empty() ? 0 : events.front().time_ns;
    replay_realtime(backend, engine, events, first_time_ns, options, start);
    metrics = engine.finish();
    physical_bytes = backend.physical_bytes_read();
    read_errors = backend.read_errors();
#else
    std::cerr << "io_uring backend not compiled in\n";
    return 2;
#endif
  }

  const uint64_t logical_bytes = metrics.logical_block_requests * options.block_bytes;
  const std::string json =
      "{\"config\":" + msaflow::config_to_json(sim_config_from(options)) +
      ",\"metrics\":" + msaflow::metrics_body_to_json(metrics) +
      ",\"storage\":" +
      storage_to_json(options.backend, physical_bytes, logical_bytes,
                      metrics.physical_block_reads, metrics.makespan_ns,
                      metrics.storage_wait_ns_total, metrics.storage_wait_events,
                      read_errors) +
      "}\n";

  if (options.out_path.empty()) {
    std::cout << json;
  } else {
    std::ofstream out(options.out_path);
    out << json;
  }
  return 0;
}
