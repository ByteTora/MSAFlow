#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "local_nvme/io_uring_backend.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace msaflow {
namespace {

uint64_t align_down(uint64_t value, uint64_t alignment) {
  return value - (value % alignment);
}

uint64_t align_up(uint64_t value, uint64_t alignment) {
  return align_down(value + alignment - 1, alignment);
}

}  // namespace

IoUringBackend::IoUringBackend(const std::string& path, const BlockDb& db, Options options)
    : db_(&db), options_(options) {
  if (options_.queue_depth == 0) {
    options_.queue_depth = 1;
  }
  if (options_.alignment == 0) {
    options_.alignment = 4096;
  }
  const int flags = options_.direct_io ? (O_RDONLY | O_DIRECT) : O_RDONLY;
  fd_ = ::open(path.c_str(), flags);
  if (fd_ < 0) {
    throw std::runtime_error("IoUringBackend: cannot open " + path + ": " +
                             std::strerror(errno));
  }
  if (options_.direct_io) {
    // Worst-case aligned span for an unaligned byte_size: round-down start and
    // round-up end can each add up to (alignment - 1).
    max_span_ = db.max_block_bytes() + 2 * options_.alignment;
    scratch_pool_ = std::make_unique<BufferPool>(max_span_, options_.queue_depth);
  }
  if (::io_uring_queue_init(static_cast<unsigned>(options_.queue_depth), &ring_, 0) < 0) {
    ::close(fd_);
    fd_ = -1;
    throw std::runtime_error("IoUringBackend: io_uring_queue_init failed");
  }
  ring_ready_ = true;
}

IoUringBackend::~IoUringBackend() {
  if (ring_ready_) {
    // Wait for outstanding operations so the kernel no longer touches our
    // buffers, but do not invoke callbacks during destruction.
    reap(false);
    while (!requests_.empty()) {
      io_uring_submit(&ring_);
      struct io_uring_cqe* cqe = nullptr;
      const int rc = io_uring_wait_cqe(&ring_, &cqe);
      if (rc < 0) {
        break;
      }
      process(cqe, false);
      io_uring_cqe_seen(&ring_, cqe);
    }
    io_uring_queue_exit(&ring_);
  }
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

uint64_t IoUringBackend::submit_read(uint64_t block_id, Buffer* buffer,
                                     ReadCallback callback) {
  const uint64_t io_id = next_io_id_++;
  auto request = std::make_unique<Request>();
  request->io_id = io_id;
  request->block_id = block_id;
  request->callback = std::move(callback);
  request->buffer = buffer;

  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    io_uring_submit(&ring_);
    sqe = io_uring_get_sqe(&ring_);
  }
  if (sqe == nullptr) {
    throw std::runtime_error("IoUringBackend: submission queue full (caller must poll)");
  }

  const BlockMetaEntry* block = db_->block(block_id);
  if (block == nullptr) {
    // Still issue a harmless nop so a completion is delivered in order; report
    // ENOENT in the callback.
    request->error = ENOENT;
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(static_cast<uintptr_t>(io_id)));
    requests_.emplace(io_id, std::move(request));
    io_uring_submit(&ring_);
    return io_id;
  }

  request->byte_size = block->byte_size;
  if (buffer->size() < block->byte_size) {
    request->error = EINVAL;
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(static_cast<uintptr_t>(io_id)));
    requests_.emplace(io_id, std::move(request));
    io_uring_submit(&ring_);
    return io_id;
  }

  if (options_.direct_io) {
    const uint64_t start = align_down(block->file_offset, options_.alignment);
    const uint64_t span = align_up(block->file_offset + block->byte_size, options_.alignment) - start;
    Buffer* scratch = scratch_pool_->acquire();
    if (scratch == nullptr || scratch->size() < span) {
      request->error = ENOMEM;
      io_uring_prep_nop(sqe);
      io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(static_cast<uintptr_t>(io_id)));
      requests_.emplace(io_id, std::move(request));
      io_uring_submit(&ring_);
      return io_id;
    }
    request->scratch = scratch;
    request->copy_offset = block->file_offset - start;
    io_uring_prep_read(sqe, fd_, scratch->data(), static_cast<unsigned>(span),
                       static_cast<off_t>(start));
  } else {
    io_uring_prep_read(sqe, fd_, buffer->data(), block->byte_size,
                       static_cast<off_t>(block->file_offset));
  }

  io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(static_cast<uintptr_t>(io_id)));
  requests_.emplace(io_id, std::move(request));
  io_uring_submit(&ring_);
  return io_id;
}

void IoUringBackend::poll() { reap(true); }

bool IoUringBackend::wait_one() {
  struct io_uring_cqe* cqe = nullptr;
  if (io_uring_wait_cqe(&ring_, &cqe) < 0) {
    return false;
  }
  process(cqe, true);
  io_uring_cqe_seen(&ring_, cqe);
  return true;
}

void IoUringBackend::reap(bool announce) {
  struct io_uring_cqe* cqe = nullptr;
  while (io_uring_peek_cqe(&ring_, &cqe) == 0) {
    process(cqe, announce);
    io_uring_cqe_seen(&ring_, cqe);
  }
}

void IoUringBackend::process(io_uring_cqe* cqe, bool announce) {
  const uint64_t io_id = static_cast<uint64_t>(
      reinterpret_cast<uintptr_t>(io_uring_cqe_get_data(cqe)));
  const auto it = requests_.find(io_id);
  if (it == requests_.end()) {
    return;
  }
  std::unique_ptr<Request> request = std::move(it->second);
  requests_.erase(it);

  int error = request->error;
  if (error == 0) {
    if (cqe->res < 0) {
      error = -cqe->res;
    } else if (options_.direct_io) {
      const uint64_t needed = request->copy_offset + request->byte_size;
      if (static_cast<uint64_t>(cqe->res) < needed) {
        error = EIO;
      } else {
        std::memcpy(request->buffer->data(),
                    static_cast<const char*>(request->scratch->data()) + request->copy_offset,
                    request->byte_size);
      }
    } else if (static_cast<uint32_t>(cqe->res) != request->byte_size) {
      error = EIO;
    }
  }

  if (request->scratch != nullptr) {
    scratch_pool_->release(request->scratch);
  }

  if (error == 0) {
    completed_reads_ += 1;
    physical_bytes_read_ += request->byte_size;
  } else {
    read_errors_ += 1;
  }
  if (announce && request->callback) {
    request->callback(request->io_id, request->block_id, error);
  }
}

}  // namespace msaflow
