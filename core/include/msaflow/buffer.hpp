#pragma once

#include <cstddef>
#include <cstdlib>
#include <utility>

namespace msaflow {

class Buffer {
 public:
  explicit Buffer(std::size_t bytes) : size_(bytes) {
    if (bytes > 0) {
      void* ptr = nullptr;
      if (posix_memalign(&ptr, kAlignment, bytes) == 0) {
        data_ = ptr;
      } else {
        size_ = 0;
      }
    }
  }

  ~Buffer() { std::free(data_); }

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  Buffer(Buffer&& other) noexcept : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }

  Buffer& operator=(Buffer&& other) noexcept {
    if (this != &other) {
      std::free(data_);
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  void* data() const { return data_; }
  std::size_t size() const { return size_; }

 private:
  static constexpr std::size_t kAlignment = 4096;

  void* data_ = nullptr;
  std::size_t size_ = 0;
};

}  // namespace msaflow
