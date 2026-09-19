#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "des.hpp"
#include "trace_reader.hpp"

namespace {

bool flag_value(int argc, char** argv, const std::string& name, std::string& out) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == name) {
      out = argv[i + 1];
      return true;
    }
  }
  return false;
}

bool parse_policy(const std::string& name, msaflow::PolicyKind& kind, std::string& canonical) {
  if (name == "fifo") {
    kind = msaflow::PolicyKind::FIFO;
  } else if (name == "lru") {
    kind = msaflow::PolicyKind::LRU;
  } else if (name == "sharing") {
    kind = msaflow::PolicyKind::SHARING_ONLY;
  } else if (name == "urgency") {
    kind = msaflow::PolicyKind::URGENCY_ONLY;
  } else if (name == "msaflow-v0") {
    kind = msaflow::PolicyKind::MSAFLOW_V0;
  } else {
    return false;
  }
  canonical = name;
  return true;
}

void usage() {
  std::cerr << "usage: msaflow-sim --trace FILE --policy "
               "{fifo,lru,sharing,urgency,msaflow-v0} --dram-blocks N "
               "[--block-bytes B] [--bandwidth-gbps X] [--io-depth D] "
               "[--prefetch on|off] [--starvation-threshold-ms MS] [--out FILE]\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string trace_path;
  std::string policy_name = "msaflow-v0";
  std::string out_path;
  if (!flag_value(argc, argv, "--trace", trace_path) || argc < 3) {
    usage();
    return 2;
  }

  msaflow::SimOptions options;
  if (!parse_policy(policy_name, options.policy, options.policy_name)) {
    std::cerr << "unknown policy: " << policy_name << "\n";
    return 2;
  }
  flag_value(argc, argv, "--policy", policy_name);
  if (!parse_policy(policy_name, options.policy, options.policy_name)) {
    std::cerr << "unknown policy: " << policy_name << "\n";
    return 2;
  }

  std::string value;
  if (flag_value(argc, argv, "--dram-blocks", value)) {
    options.dram_blocks = std::strtoull(value.c_str(), nullptr, 10);
  }
  msaflow::Trace trace = msaflow::read_trace(trace_path);
  if (flag_value(argc, argv, "--block-bytes", value)) {
    options.block_bytes = std::strtoull(value.c_str(), nullptr, 10);
  } else if (trace.config.block_bytes > 0) {
    options.block_bytes = trace.config.block_bytes;
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
  flag_value(argc, argv, "--out", out_path);

  const msaflow::SimMetrics metrics = msaflow::run_simulation(trace, options);
  const msaflow::SimConfig config = msaflow::config_from_options(options);

  const std::string json = msaflow::metrics_to_json(config, metrics) + "\n";
  if (out_path.empty()) {
    std::cout << json;
  } else {
    std::ofstream out(out_path);
    out << json;
  }
  return 0;
}
