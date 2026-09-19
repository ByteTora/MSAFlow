#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace msaflow {

struct BlockRequest {
  uint64_t query_id = 0;
  uint64_t block_id = 0;
};

using ConsumerMap = std::map<uint64_t, std::vector<uint64_t>>;

inline ConsumerMap aggregate_requests(const std::vector<BlockRequest>& requests) {
  ConsumerMap consumers;
  for (const BlockRequest& request : requests) {
    consumers[request.block_id].push_back(request.query_id);
  }
  return consumers;
}

}  // namespace msaflow
