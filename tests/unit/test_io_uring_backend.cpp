#include <gtest/gtest.h>

#ifdef MSAFLOW_HAVE_IO_URING

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "msaflow/block_db.hpp"
#include "msaflow/buffer.hpp"
#include "local_nvme/io_uring_backend.hpp"

namespace msaflow {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kBlockBytes = 4096;
constexpr uint64_t kBlockCount = 4;

std::vector<char> make_payload(uint64_t bytes) {
  std::vector<char> data(bytes);
  for (uint64_t i = 0; i < bytes; ++i) {
    data[i] = static_cast<char>('A' + (i * 7 + 3) % 26);
  }
  return data;
}

class TempBlockDb {
 public:
  explicit TempBlockDb(uint64_t block_count = kBlockCount, uint64_t block_bytes = kBlockBytes)
      : block_count_(block_count), block_bytes_(block_bytes) {
    std::string tmpl = (fs::temp_directory_path() / "msaflow_uring_XXXXXX").string();
    std::vector<char> buffer(tmpl.begin(), tmpl.end());
    buffer.push_back('\0');
    const char* created = ::mkdtemp(buffer.data());
    if (created == nullptr) {
      throw std::runtime_error("mkdtemp failed");
    }
    dir_ = created;
    write_files();
  }

  ~TempBlockDb() { fs::remove_all(dir_); }

  const std::string& dir() const { return dir_; }
  uint64_t block_count() const { return block_count_; }
  uint64_t block_bytes() const { return block_bytes_; }
  const std::vector<char>& payload() const { return payload_; }

 private:
  void write_files() {
    const uint64_t logical = block_count_ * block_bytes_;
    payload_ = make_payload(logical);
    const uint64_t padded = (logical + 4095) / 4096 * 4096;
    {
      std::ofstream data(fs::path(dir_) / "sequences.data", std::ios::binary);
      data.write(payload_.data(), static_cast<std::streamsize>(payload_.size()));
      if (padded > logical) {
        std::vector<char> zeros(static_cast<std::size_t>(padded - logical), '\0');
        data.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
      }
    }
    std::ofstream index(fs::path(dir_) / "sequences.index");
    std::ofstream blocks(fs::path(dir_) / "blocks.meta");
    for (uint64_t i = 0; i < block_count_; ++i) {
      index << i << " " << (i * block_bytes_) << " " << block_bytes_ << " " << i << "\n";
      blocks << i << " " << (i * block_bytes_) << " " << block_bytes_ << " " << i << " " << i
             << "\n";
    }
    std::ofstream manifest(fs::path(dir_) / "manifest.json");
    manifest << "{\n"
             << "  \"format_version\": 1,\n"
             << "  \"database_id\": 1,\n"
             << "  \"database_version\": {\"major\": 1, \"minor\": 0},\n"
             << "  \"sequence_count\": " << block_count_ << ",\n"
             << "  \"block_count\": " << block_count_ << ",\n"
             << "  \"block_size\": " << block_bytes_ << ",\n"
             << "  \"total_bytes\": " << logical << ",\n"
             << "  \"file_bytes\": " << padded << ",\n"
             << "  \"checksum\": \"sha256:test\"\n"
             << "}\n";
  }

  std::string dir_;
  uint64_t block_count_;
  uint64_t block_bytes_;
  std::vector<char> payload_;
};

std::string expected_block(const TempBlockDb& db, uint64_t block_id) {
  return std::string(db.payload().data() + block_id * db.block_bytes(), db.block_bytes());
}

template <typename Callable>
bool try_open_backend(const std::string& path, const BlockDb& db,
                      IoUringBackend::Options options, Callable&& body) {
  try {
    IoUringBackend backend(path, db, options);
    body(backend);
    return true;
  } catch (const std::runtime_error&) {
    return false;
  }
}

TEST(IoUringBackend, BufferedReadsMatchBlockBytes) {
  TempBlockDb fixture;
  const BlockDb db = BlockDb::open(fixture.dir());
  const bool ran = try_open_backend(
      db.data_path(), db, IoUringBackend::Options(4, false, 4096),
      [&](IoUringBackend& backend) {
        std::vector<Buffer> buffers;
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          buffers.emplace_back(kBlockBytes);
        }
        std::vector<std::string> results(fixture.block_count());
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          backend.submit_read(i, &buffers[i], [&, i](uint64_t, uint64_t, int err) {
            EXPECT_EQ(err, 0);
            results[i] = std::string(static_cast<char*>(buffers[i].data()), kBlockBytes);
          });
        }
        while (backend.pending_count() > 0) {
          backend.wait_one();
        }
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          EXPECT_EQ(results[i], expected_block(fixture, i));
        }
        EXPECT_EQ(backend.read_errors(), 0u);
      });
  if (!ran) {
    GTEST_SKIP() << "io_uring unavailable in this environment";
  }
}

TEST(IoUringBackend, DirectIoReadsMatchBlockBytes) {
  TempBlockDb fixture;
  const BlockDb db = BlockDb::open(fixture.dir());
  const bool ran = try_open_backend(
      db.data_path(), db, IoUringBackend::Options(4, true, 4096),
      [&](IoUringBackend& backend) {
        std::vector<Buffer> buffers;
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          buffers.emplace_back(kBlockBytes);
        }
        std::vector<std::string> results(fixture.block_count());
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          backend.submit_read(i, &buffers[i], [&, i](uint64_t, uint64_t, int err) {
            EXPECT_EQ(err, 0);
            results[i] = std::string(static_cast<char*>(buffers[i].data()), kBlockBytes);
          });
        }
        while (backend.pending_count() > 0) {
          backend.wait_one();
        }
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          EXPECT_EQ(results[i], expected_block(fixture, i));
        }
      });
  if (!ran) {
    GTEST_SKIP() << "io_uring unavailable in this environment";
  }
}

TEST(IoUringBackend, DirectIoHandlesUnalignedBlocks) {
  // Blocks of 100 bytes at unaligned offsets force the aligned scratch span to
  // exceed the block size; this guards the scratch-pool sizing regression.
  TempBlockDb fixture(3, 100);
  const BlockDb db = BlockDb::open(fixture.dir());
  const bool ran = try_open_backend(
      db.data_path(), db, IoUringBackend::Options(4, true, 4096),
      [&](IoUringBackend& backend) {
        std::vector<Buffer> buffers;
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          buffers.emplace_back(fixture.block_bytes());
        }
        std::vector<std::string> results(fixture.block_count());
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          backend.submit_read(i, &buffers[i], [&, i](uint64_t, uint64_t, int err) {
            EXPECT_EQ(err, 0);
            results[i] = std::string(static_cast<char*>(buffers[i].data()),
                                     fixture.block_bytes());
          });
        }
        while (backend.pending_count() > 0) {
          backend.wait_one();
        }
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          EXPECT_EQ(results[i], expected_block(fixture, i));
        }
        EXPECT_EQ(backend.read_errors(), 0u);
      });
  if (!ran) {
    GTEST_SKIP() << "io_uring unavailable in this environment";
  }
}

TEST(IoUringBackend, SubmissionsOverlapBeforePolling) {
  TempBlockDb fixture;
  const BlockDb db = BlockDb::open(fixture.dir());
  const bool ran = try_open_backend(
      db.data_path(), db, IoUringBackend::Options(4, false, 4096),
      [&](IoUringBackend& backend) {
        std::vector<Buffer> buffers;
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          buffers.emplace_back(kBlockBytes);
        }
        int calls = 0;
        for (uint64_t i = 0; i < fixture.block_count(); ++i) {
          backend.submit_read(i, &buffers[i], [&](uint64_t, uint64_t, int) { calls += 1; });
        }
        EXPECT_EQ(backend.pending_count(), fixture.block_count());
        EXPECT_EQ(calls, 0);
        while (backend.pending_count() > 0) {
          backend.wait_one();
        }
        EXPECT_EQ(calls, static_cast<int>(fixture.block_count()));
      });
  if (!ran) {
    GTEST_SKIP() << "io_uring unavailable in this environment";
  }
}

TEST(IoUringBackend, UnknownBlockReportsError) {
  TempBlockDb fixture;
  const BlockDb db = BlockDb::open(fixture.dir());
  const bool ran = try_open_backend(
      db.data_path(), db, IoUringBackend::Options(2, false, 4096),
      [&](IoUringBackend& backend) {
        Buffer buffer(kBlockBytes);
        int error = 0;
        backend.submit_read(99, &buffer, [&](uint64_t, uint64_t, int err) { error = err; });
        while (backend.pending_count() > 0) {
          backend.wait_one();
        }
        EXPECT_EQ(error, ENOENT);
        EXPECT_EQ(backend.read_errors(), 1u);
      });
  if (!ran) {
    GTEST_SKIP() << "io_uring unavailable in this environment";
  }
}

}  // namespace
}  // namespace msaflow

#endif  // MSAFLOW_HAVE_IO_URING
