#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "msaflow/types.hpp"

namespace msaflow {

class QueryRegistry {
 public:
  void set_expected_version(const DatabaseId& db_id, const DatabaseVersion& version) {
    expected_versions_[db_id.id] = version;
  }

  bool register_query(const QueryState& query) {
    if (queries_.find(query.query_id) != queries_.end()) {
      return false;
    }
    const auto expected = expected_versions_.find(query.db_id.id);
    if (expected != expected_versions_.end() && !(expected->second == query.db_version)) {
      return false;
    }
    queries_.emplace(query.query_id, query);
    return true;
  }

  const QueryState* get(uint64_t query_id) const {
    const auto it = queries_.find(query_id);
    return it == queries_.end() ? nullptr : &it->second;
  }

  bool advance_cursor(uint64_t query_id, uint64_t current_block, uint64_t next_block,
                      uint64_t now_ns) {
    const auto it = queries_.find(query_id);
    if (it == queries_.end()) {
      return false;
    }
    it->second.current_block = current_block;
    it->second.next_block = next_block;
    it->second.blocks_processed += 1;
    it->second.last_progress_ns = now_ns;
    return true;
  }

  bool set_status(uint64_t query_id, QueryStatus status) {
    const auto it = queries_.find(query_id);
    if (it == queries_.end()) {
      return false;
    }
    it->second.status = status;
    return true;
  }

  bool cancel(uint64_t query_id) { return set_status(query_id, QueryStatus::CANCELLED); }

  std::size_t size() const { return queries_.size(); }

  std::vector<uint64_t> runnable_ids() const {
    std::vector<uint64_t> ids;
    for (const auto& [query_id, query] : queries_) {
      if (query.status == QueryStatus::RUNNABLE || query.status == QueryStatus::WAITING_BLOCK) {
        ids.push_back(query_id);
      }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
  }

 private:
  std::unordered_map<uint64_t, QueryState> queries_;
  std::unordered_map<uint64_t, DatabaseVersion> expected_versions_;
};

}  // namespace msaflow
