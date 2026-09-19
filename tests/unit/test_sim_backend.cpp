#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "msaflow/buffer.hpp"
#include "simulated_backend.hpp"

namespace msaflow {
namespace {

struct Completion {
  uint64_t io_id = 0;
  uint64_t block_id = 0;
  int err = 0;
  uint64_t time_ns = 0;
};

SimulatedBackend make_backend(uint64_t queue_depth) {
  return SimulatedBackend(SimulatedBackendConfig{1000, 1.0, 10, queue_depth});
}

ReadCallback recorder(SimulatedBackend& backend, std::vector<Completion>& done) {
  return [&backend, &done](uint64_t io_id, uint64_t block_id, int err) {
    done.push_back(Completion{io_id, block_id, err, backend.now_ns()});
  };
}

TEST(SimulatedBackend, QueueDepthOneSerializes) {
  SimulatedBackend backend = make_backend(1);
  Buffer first(1000);
  Buffer second(1000);
  std::vector<Completion> done;
  const ReadCallback cb = recorder(backend, done);
  backend.submit_read(1, &first, cb);
  backend.submit_read(2, &second, cb);
  backend.advance_to(100000);
  ASSERT_EQ(done.size(), 2u);
  EXPECT_EQ(done[0].block_id, 1u);
  EXPECT_EQ(done[1].block_id, 2u);
  EXPECT_LT(done[0].time_ns, done[1].time_ns);
}

TEST(SimulatedBackend, QueueDepthTwoOverlaps) {
  SimulatedBackend backend = make_backend(2);
  Buffer first(1000);
  Buffer second(1000);
  std::vector<Completion> done;
  const ReadCallback cb = recorder(backend, done);
  backend.submit_read(1, &first, cb);
  backend.submit_read(2, &second, cb);
  backend.advance_to(100000);
  ASSERT_EQ(done.size(), 2u);
  EXPECT_EQ(done[0].time_ns, done[1].time_ns);
}

TEST(SimulatedBackend, DoesNotCompleteBeforeTime) {
  SimulatedBackend backend = make_backend(1);
  Buffer buffer(1000);
  std::vector<Completion> done;
  backend.submit_read(7, &buffer, recorder(backend, done));
  backend.advance_to(5);
  EXPECT_TRUE(done.empty());
  backend.advance_to(2000);
  EXPECT_EQ(done.size(), 1u);
}

TEST(SimulatedBackend, InjectedErrorIsDelivered) {
  SimulatedBackend backend = make_backend(1);
  Buffer first(1000);
  Buffer second(1000);
  std::vector<Completion> done;
  const ReadCallback cb = recorder(backend, done);
  backend.fail_next_read_with(5);
  backend.submit_read(1, &first, cb);
  backend.submit_read(2, &second, cb);
  backend.advance_to(100000);
  ASSERT_EQ(done.size(), 2u);
  EXPECT_EQ(done[0].err, 5);
  EXPECT_EQ(done[1].err, 0);
}

TEST(SimulatedBackend, IoIdsAreSequential) {
  SimulatedBackend backend = make_backend(4);
  Buffer buffer(1000);
  std::vector<Completion> done;
  const ReadCallback cb = recorder(backend, done);
  EXPECT_EQ(backend.submit_read(1, &buffer, cb), 0u);
  EXPECT_EQ(backend.submit_read(2, &buffer, cb), 1u);
}

}  // namespace
}  // namespace msaflow
