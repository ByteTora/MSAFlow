#pragma once

#ifdef MSAFLOW_HAVE_IO_URING

#include <liburing.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "msaflow/block_db.hpp"
#include "msaflow/buffer.hpp"
#include "msaflow/buffer_pool.hpp"
#include "msaflow/io_backend.hpp"

namespace msaflow {

// Asynchronous file backend over Linux io_uring (Phase 2B).
//
// submit_read queues an IORING_OP_READ into the submission ring and returns
// immediately; poll() reaps completion queue entries and invokes callbacks.
// With O_DIRECT enabled (default) each read goes through a 4K-aligned scratch
// buffer acquired from a pool sized to the queue depth, then the requested byte
// range is copied into the caller's buffer. This keeps arbitrary (unaligned)
// Block DB blocks readable while bypassing the page cache.
//
// Construction throws std::runtime_error if the ring cannot be set up (for
// example when io_uring is restricted in a container); callers should treat that
// as "backend unavailable".
class IoUringBackend : public StorageBackend {
 public:
  struct Options {
    unsigned queue_depth;
    bool direct_io;
    std::size_t alignment;

    Options() : queue_depth(32), direct_io(true), alignment(4096) {}
    Options(unsigned depth, bool direct, std::size_t align)
        : queue_depth(depth), direct_io(direct), alignment(align) {}
  };

  IoUringBackend(const std::string& path, const BlockDb& db, Options options = Options());
  ~IoUringBackend() override;

  IoUringBackend(const IoUringBackend&) = delete;
  IoUringBackend& operator=(const IoUringBackend&) = delete;

  uint64_t submit_read(uint64_t block_id, Buffer* buffer, ReadCallback callback) override;
  void poll() override;

  // Blocks until one completion is processed (used by replay drivers that need
  // to make progress without busy-waiting). Returns false on ring error.
  bool wait_one();

  uint64_t completed_reads() const { return completed_reads_; }
  uint64_t physical_bytes_read() const { return physical_bytes_read_; }
  uint64_t read_errors() const { return read_errors_; }
  std::size_t pending_count() const { return requests_.size(); }

 private:
  struct Request {
    uint64_t io_id = 0;
    uint64_t block_id = 0;
    int error = 0;
    ReadCallback callback;
    Buffer* buffer = nullptr;
    Buffer* scratch = nullptr;
    uint64_t copy_offset = 0;
    uint32_t byte_size = 0;
  };

  void process(io_uring_cqe* cqe, bool announce);
  void reap(bool announce);

  int fd_ = -1;
  const BlockDb* db_ = nullptr;
  Options options_;
  io_uring ring_{};
  bool ring_ready_ = false;
  uint64_t next_io_id_ = 1;
  uint64_t completed_reads_ = 0;
  uint64_t physical_bytes_read_ = 0;
  uint64_t read_errors_ = 0;
  std::size_t max_span_ = 0;
  std::unique_ptr<BufferPool> scratch_pool_;
  std::unordered_map<uint64_t, std::unique_ptr<Request>> requests_;
};

}  // namespace msaflow

#endif  // MSAFLOW_HAVE_IO_URING
