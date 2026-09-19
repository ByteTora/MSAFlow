#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "sync/pread_backend.hpp"

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

PreadBackend::PreadBackend(const std::string& path, const BlockDb& db, Options options)
    : db_(&db), options_(options) {
  if (options_.alignment == 0) {
    options_.alignment = 4096;
  }
  const int flags = options_.direct_io ? (O_RDONLY | O_DIRECT) : O_RDONLY;
  fd_ = ::open(path.c_str(), flags);
  if (fd_ < 0) {
    throw std::runtime_error("PreadBackend: cannot open " + path + ": " + std::strerror(errno));
  }
}

PreadBackend::~PreadBackend() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

uint64_t PreadBackend::submit_read(uint64_t block_id, Buffer* buffer, ReadCallback callback) {
  const uint64_t io_id = next_io_id_++;
  Completion completion;
  completion.io_id = io_id;
  completion.block_id = block_id;
  completion.callback = std::move(callback);

  const BlockMetaEntry* block = db_->block(block_id);
  if (block == nullptr) {
    completion.error = ENOENT;
    read_errors_ += 1;
  } else {
    completion.error = options_.direct_io ? read_direct(*block, buffer)
                                          : read_buffered(*block, buffer);
    if (completion.error == 0) {
      completed_reads_ += 1;
      physical_bytes_read_ += block->byte_size;
    } else {
      read_errors_ += 1;
    }
  }
  pending_.push_back(std::move(completion));
  return io_id;
}

void PreadBackend::poll() {
  std::vector<Completion> ready = std::move(pending_);
  pending_.clear();
  for (const Completion& completion : ready) {
    if (completion.callback) {
      completion.callback(completion.io_id, completion.block_id, completion.error);
    }
  }
}

bool PreadBackend::wait_one() {
  if (pending_.empty()) {
    return false;
  }
  poll();
  return true;
}

int PreadBackend::read_buffered(const BlockMetaEntry& block, Buffer* buffer) {
  if (buffer->size() < block.byte_size) {
    return EINVAL;
  }
  std::size_t done = 0;
  while (done < block.byte_size) {
    const ssize_t n = ::pread(fd_, static_cast<char*>(buffer->data()) + done,
                              block.byte_size - done,
                              static_cast<off_t>(block.file_offset + done));
    if (n < 0) {
      return errno;
    }
    if (n == 0) {
      return EIO;
    }
    done += static_cast<std::size_t>(n);
  }
  return 0;
}

int PreadBackend::read_direct(const BlockMetaEntry& block, Buffer* buffer) {
  const uint64_t alignment = options_.alignment;
  const uint64_t start = align_down(block.file_offset, alignment);
  const uint64_t span = align_up(block.file_offset + block.byte_size, alignment) - start;
  if (direct_scratch_ == nullptr || direct_scratch_->size() < span) {
    direct_scratch_ = std::make_unique<Buffer>(span);
    if (direct_scratch_->size() < span) {
      return ENOMEM;
    }
  }
  const ssize_t n = ::pread(fd_, direct_scratch_->data(), span, static_cast<off_t>(start));
  if (n < 0) {
    return errno;
  }
  const uint64_t needed = block.file_offset - start + block.byte_size;
  if (static_cast<uint64_t>(n) < needed) {
    return EIO;
  }
  if (buffer->size() < block.byte_size) {
    return EINVAL;
  }
  std::memcpy(buffer->data(),
              static_cast<const char*>(direct_scratch_->data()) + (block.file_offset - start),
              block.byte_size);
  return 0;
}

}  // namespace msaflow
