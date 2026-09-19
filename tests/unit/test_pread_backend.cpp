#include <gtest/gtest.h>

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
#include "sync/pread_backend.hpp"

namespace msaflow {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kBlockBytes = 4096;
constexpr uint64_t kBlockCount = 2;

std::vector<char> make_payload(uint64_t bytes) {
  std::vector<char> data(bytes);
  for (uint64_t i = 0; i < bytes; ++i) {
    data[i] = static_cast<char>('A' + (i * 7 + 3) % 26);
  }
  return data;
}

class TempBlockDb {
 public:
  explicit TempBlockDb(uint64_t block_count = kBlockCount) : block_count_(block_count) {
    std::string tmpl = (fs::temp_directory_path() / "msaflow_pread_XXXXXX").string();
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
  const std::vector<char>& payload() const { return payload_; }

 private:
  void write_files() {
    payload_ = make_payload(block_count_ * kBlockBytes);
    {
      std::ofstream data(fs::path(dir_) / "sequences.data", std::ios::binary);
      data.write(payload_.data(), static_cast<std::streamsize>(payload_.size()));
    }
    std::ofstream index(fs::path(dir_) / "sequences.index");
    std::ofstream blocks(fs::path(dir_) / "blocks.meta");
    for (uint64_t i = 0; i < block_count_; ++i) {
      index << i << " " << (i * kBlockBytes) << " " << kBlockBytes << " " << i << "\n";
      blocks << i << " " << (i * kBlockBytes) << " " << kBlockBytes << " " << i << " " << i
             << "\n";
    }
    std::ofstream manifest(fs::path(dir_) / "manifest.json");
    manifest << "{\n"
             << "  \"format_version\": 1,\n"
             << "  \"database_id\": 1,\n"
             << "  \"database_version\": {\"major\": 1, \"minor\": 0},\n"
             << "  \"sequence_count\": " << block_count_ << ",\n"
             << "  \"block_count\": " << block_count_ << ",\n"
             << "  \"block_size\": " << kBlockBytes << ",\n"
             << "  \"total_bytes\": " << (block_count_ * kBlockBytes) << ",\n"
             << "  \"checksum\": \"sha256:test\"\n"
             << "}\n";
  }

  std::string dir_;
  uint64_t block_count_;
  std::vector<char> payload_;
};

std::string expected_block(const TempBlockDb& db, uint64_t block_id) {
  return std::string(db.payload().data() + block_id * kBlockBytes, kBlockBytes);
}

bool odirect_supported(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECT);
  if (fd < 0) {
    return false;
  }
  ::close(fd);
  return true;
}

struct CallbackRecord {
  uint64_t io_id = 0;
  uint64_t block_id = 0;
  int error = 0;
  int calls = 0;
};

TEST(PreadBackend, BufferedReadsMatchBlockBytes) {
  TempBlockDb fixture;
  const BlockDb db = BlockDb::open(fixture.dir());
  PreadBackend backend(db.data_path(), db);
  Buffer buffer(kBlockBytes);

  CallbackRecord first;
  backend.submit_read(0, &buffer, [&first](uint64_t id, uint64_t block, int err) {
    first.io_id = id;
    first.block_id = block;
    first.error = err;
    first.calls += 1;
  });
  EXPECT_EQ(first.calls, 0);
  backend.poll();
  EXPECT_EQ(first.calls, 1);
  EXPECT_EQ(first.error, 0);
  EXPECT_EQ(std::string(static_cast<char*>(buffer.data()), kBlockBytes), expected_block(fixture, 0));
  EXPECT_EQ(backend.completed_reads(), 1u);
  EXPECT_EQ(backend.physical_bytes_read(), kBlockBytes);
}

TEST(PreadBackend, PollDeliversAllQueuedCompletions) {
  TempBlockDb fixture;
  const BlockDb db = BlockDb::open(fixture.dir());
  PreadBackend backend(db.data_path(), db);

  std::vector<Buffer> buffers;
  for (uint64_t i = 0; i < fixture.block_count(); ++i) {
    buffers.emplace_back(kBlockBytes);
  }
  int calls = 0;
  uint64_t last_id = 0;
  for (uint64_t i = 0; i < fixture.block_count(); ++i) {
    const uint64_t io_id = backend.submit_read(
        i, &buffers[i], [&](uint64_t id, uint64_t, int err) {
          EXPECT_EQ(err, 0);
          calls += 1;
          last_id = id;
        });
    EXPECT_EQ(io_id, i + 1);
  }
  backend.poll();
  EXPECT_EQ(calls, static_cast<int>(fixture.block_count()));
  EXPECT_EQ(last_id, fixture.block_count());
  for (uint64_t i = 0; i < fixture.block_count(); ++i) {
    EXPECT_EQ(std::string(static_cast<char*>(buffers[i].data()), kBlockBytes),
              expected_block(fixture, i));
  }
}

TEST(PreadBackend, UnknownBlockReportsError) {
  TempBlockDb fixture;
  const BlockDb db = BlockDb::open(fixture.dir());
  PreadBackend backend(db.data_path(), db);
  Buffer buffer(kBlockBytes);
  int error = 0;
  int calls = 0;
  backend.submit_read(99, &buffer, [&](uint64_t, uint64_t, int err) {
    error = err;
    calls += 1;
  });
  backend.poll();
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(error, ENOENT);
  EXPECT_EQ(backend.read_errors(), 1u);
}

TEST(PreadBackend, DirectIoReadsMatchBlockBytes) {
  TempBlockDb fixture;
  if (!odirect_supported((fs::path(fixture.dir()) / "sequences.data").string())) {
    GTEST_SKIP() << "O_DIRECT not supported on this filesystem";
  }
  const BlockDb db = BlockDb::open(fixture.dir());
  PreadBackend::Options options;
  options.direct_io = true;
  PreadBackend backend(db.data_path(), db, options);
  Buffer buffer(kBlockBytes);

  std::vector<std::string> results(fixture.block_count());
  for (uint64_t i = 0; i < fixture.block_count(); ++i) {
    backend.submit_read(i, &buffer, [&, i](uint64_t, uint64_t, int err) {
      EXPECT_EQ(err, 0);
      results[i] = std::string(static_cast<char*>(buffer.data()), kBlockBytes);
    });
    backend.poll();
  }
  for (uint64_t i = 0; i < fixture.block_count(); ++i) {
    EXPECT_EQ(results[i], expected_block(fixture, i));
  }
}

}  // namespace
}  // namespace msaflow
