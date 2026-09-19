#pragma once

#include <cstdint>
#include <functional>

#include "msaflow/buffer.hpp"

namespace msaflow {

using ReadCallback = std::function<void(uint64_t io_id, uint64_t block_id, int err)>;

class StorageBackend {
 public:
  virtual ~StorageBackend() = default;

  virtual uint64_t submit_read(uint64_t block_id, Buffer* buffer, ReadCallback callback) = 0;

  virtual void poll() = 0;
};

}  // namespace msaflow
