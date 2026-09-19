#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "msaflow/block_db.hpp"
#include "msaflow/buffer.hpp"
#include "msaflow/io_backend.hpp"

namespace msaflow {

// Synchronous file backend behind the StorageBackend interface. It maps a block
// id to (file_offset, byte_size) via the Block DB and reads with pread. This
// makes the runtime testable without io_uring and provides the "page cache"
// baseline; a later option adds O_DIRECT.
//
// submit_read issues the read immediately and queues the completion; poll()
// delivers queued completions. Callbacks must not assume they are invoked from
// within submit_read.
class PreadBackend : public StorageBackend {
 public:
  struct Options {
    bool direct_io;      // O_DIRECT (bypasses page cache); needs alignment
    std::size_t alignment;

    Options() : direct_io(false), alignment(4096) {}
    Options(bool direct, std::size_t align) : direct_io(direct), alignment(align) {}
  };

  PreadBackend(const std::string& path, const BlockDb& db, Options options = Options());
  ~PreadBackend() override;

  PreadBackend(const PreadBackend&) = delete;
  PreadBackend& operator=(const PreadBackend&) = delete;

  uint64_t submit_read(uint64_t block_id, Buffer* buffer, ReadCallback callback) override;
  void poll() override;

  // Compatibility with the io_uring backend's blocking-reap API: pread reads are
  // synchronous, so this simply delivers everything queued.
  bool wait_one();

  uint64_t completed_reads() const { return completed_reads_; }
  uint64_t physical_bytes_read() const { return physical_bytes_read_; }
  uint64_t read_errors() const { return read_errors_; }
  std::size_t pending_count() const { return pending_.size(); }

 private:
  struct Completion {
    uint64_t io_id = 0;
    uint64_t block_id = 0;
    int error = 0;
    ReadCallback callback;
  };

  int read_buffered(const BlockMetaEntry& block, Buffer* buffer);
  int read_direct(const BlockMetaEntry& block, Buffer* buffer);

  int fd_ = -1;
  const BlockDb* db_ = nullptr;
  Options options_;
  uint64_t next_io_id_ = 1;
  uint64_t completed_reads_ = 0;
  uint64_t physical_bytes_read_ = 0;
  uint64_t read_errors_ = 0;
  std::vector<Completion> pending_;
  std::unique_ptr<Buffer> direct_scratch_;
};

}  // namespace msaflow
