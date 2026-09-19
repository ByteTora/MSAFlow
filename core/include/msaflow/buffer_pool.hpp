#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "msaflow/buffer.hpp"

namespace msaflow {

// Fixed-capacity pool of 4K-aligned buffers. Single-threaded for V0; the future
// runtime can add locking or per-core pools behind the same acquire/release API.
class BufferPool {
 public:
  BufferPool(std::size_t buffer_bytes, std::size_t count) {
    buffers_.reserve(count);
    free_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      buffers_.push_back(std::make_unique<Buffer>(buffer_bytes));
      free_.push_back(buffers_.back().get());
    }
  }

  Buffer* acquire() {
    if (free_.empty()) {
      return nullptr;
    }
    Buffer* buffer = free_.back();
    free_.pop_back();
    return buffer;
  }

  void release(Buffer* buffer) { free_.push_back(buffer); }

  std::size_t free_count() const { return free_.size(); }
  std::size_t capacity() const { return buffers_.size(); }
  std::size_t buffer_bytes() const {
    return buffers_.empty() ? 0 : buffers_.front()->size();
  }

 private:
  std::vector<std::unique_ptr<Buffer>> buffers_;
  std::vector<Buffer*> free_;
};

}  // namespace msaflow
