#include "des.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "msaflow/replay_engine.hpp"
#include "simulated_backend.hpp"

namespace msaflow {
namespace {

constexpr uint64_t kNoNextBlock = static_cast<uint64_t>(-1);

struct Event {
  uint64_t time_ns = 0;
  uint64_t query_id = 0;
  uint64_t block_id = 0;
  uint64_t next_block = kNoNextBlock;
};

ReplayOptions replay_options_from(const SimOptions& options) {
  ReplayOptions replay;
  replay.policy = options.policy;
  replay.policy_name = options.policy_name;
  replay.dram_blocks = options.dram_blocks;
  replay.block_bytes = options.block_bytes;
  replay.bandwidth_bytes_per_ns = options.bandwidth_bytes_per_ns;
  replay.base_latency_ns = options.base_latency_ns;
  replay.io_depth = options.io_depth;
  replay.prefetch = options.prefetch;
  replay.coalesce_inflight = options.coalesce_inflight;
  replay.starvation_threshold_ns = options.starvation_threshold_ns;
  replay.scheduler_decision_cost_ns = options.scheduler_decision_cost_ns;
  return replay;
}

std::vector<Event> build_events(const Trace& trace) {
  std::vector<Event> events;
  for (const TraceQuery& query : trace.queries) {
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

}  // namespace

SimMetrics run_simulation(const Trace& trace, const SimOptions& options) {
  const uint64_t queue_depth = options.io_depth < 1 ? 1 : options.io_depth;
  SimulatedBackend backend(SimulatedBackendConfig{options.block_bytes,
                                                   options.bandwidth_bytes_per_ns,
                                                   options.base_latency_ns, queue_depth});
  ReplayEngine engine(backend, replay_options_from(options),
                      [&backend]() { return backend.now_ns(); });

  for (const TraceQuery& query : trace.queries) {
    engine.register_query(query.query_id);
  }

  const std::vector<Event> events = build_events(trace);
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

  return engine.finish();
}

SimConfig config_from_options(const SimOptions& options) {
  SimConfig config;
  config.policy = options.policy_name;
  config.dram_blocks = options.dram_blocks;
  config.block_bytes = options.block_bytes;
  config.bandwidth_bytes_per_ns = options.bandwidth_bytes_per_ns;
  config.base_latency_ns = options.base_latency_ns;
  config.io_depth = options.io_depth;
  config.prefetch = options.prefetch;
  config.coalesce_inflight = options.coalesce_inflight;
  config.starvation_threshold_ns = options.starvation_threshold_ns;
  return config;
}

}  // namespace msaflow
