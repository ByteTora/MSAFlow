#include <gtest/gtest.h>

#include "msaflow/query_registry.hpp"

namespace msaflow {
namespace {

QueryState make_query(uint64_t query_id, uint64_t major = 1, uint64_t minor = 0) {
  QueryState query;
  query.query_id = query_id;
  query.db_id = DatabaseId{1};
  query.db_version = DatabaseVersion{major, minor};
  return query;
}

TEST(QueryRegistry, RegisterAndGet) {
  QueryRegistry registry;
  ASSERT_TRUE(registry.register_query(make_query(7)));
  ASSERT_EQ(registry.size(), 1u);
  const QueryState* state = registry.get(7);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->query_id, 7u);
  EXPECT_EQ(registry.get(8), nullptr);
}

TEST(QueryRegistry, DuplicateIdRejected) {
  QueryRegistry registry;
  ASSERT_TRUE(registry.register_query(make_query(1)));
  EXPECT_FALSE(registry.register_query(make_query(1)));
  EXPECT_EQ(registry.size(), 1u);
}

TEST(QueryRegistry, VersionMismatchRejected) {
  QueryRegistry registry;
  registry.set_expected_version(DatabaseId{1}, DatabaseVersion{2, 0});
  EXPECT_FALSE(registry.register_query(make_query(1, 1, 0)));
  EXPECT_TRUE(registry.register_query(make_query(2, 2, 0)));
  EXPECT_EQ(registry.size(), 1u);
}

TEST(QueryRegistry, AdvanceCursorTracksProgress) {
  QueryRegistry registry;
  ASSERT_TRUE(registry.register_query(make_query(3)));
  ASSERT_TRUE(registry.advance_cursor(3, 10, 11, 500));
  const QueryState* state = registry.get(3);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->current_block, 10u);
  EXPECT_EQ(state->next_block, 11u);
  EXPECT_EQ(state->blocks_processed, 1u);
  EXPECT_EQ(state->last_progress_ns, 500u);
  EXPECT_FALSE(registry.advance_cursor(99, 0, 1, 0));
}

TEST(QueryRegistry, StatusAndCancel) {
  QueryRegistry registry;
  ASSERT_TRUE(registry.register_query(make_query(4)));
  ASSERT_TRUE(registry.set_status(4, QueryStatus::RUNNABLE));
  EXPECT_EQ(registry.get(4)->status, QueryStatus::RUNNABLE);
  ASSERT_TRUE(registry.cancel(4));
  EXPECT_EQ(registry.get(4)->status, QueryStatus::CANCELLED);
  EXPECT_FALSE(registry.cancel(99));
}

TEST(QueryRegistry, RunnableIdsAreSorted) {
  QueryRegistry registry;
  ASSERT_TRUE(registry.register_query(make_query(9)));
  ASSERT_TRUE(registry.register_query(make_query(2)));
  ASSERT_TRUE(registry.register_query(make_query(5)));
  ASSERT_TRUE(registry.set_status(9, QueryStatus::RUNNABLE));
  ASSERT_TRUE(registry.set_status(2, QueryStatus::WAITING_BLOCK));
  const std::vector<uint64_t> ids = registry.runnable_ids();
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], 2u);
  EXPECT_EQ(ids[1], 9u);
}

}  // namespace
}  // namespace msaflow
