#pragma once

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace msaflow {

// Reader for the MSAFlow Block Database format (spec §6), written by
// database/builder/block_db.py. Header-only and dependency-free so both the
// simulator and the local storage runtime can consume the same on-disk format.
//
// On-disk layout: manifest.json, sequences.data, sequences.index, blocks.meta.
// A sequence never crosses a block boundary (verified by verify_block_db.py).

struct BlockDbManifest {
  uint64_t format_version = 0;
  uint64_t database_id = 0;
  uint64_t version_major = 0;
  uint64_t version_minor = 0;
  uint64_t sequence_count = 0;
  uint64_t block_count = 0;
  uint64_t block_size = 0;
  uint64_t total_bytes = 0;
  std::string checksum;
};

struct SequenceIndexEntry {
  uint64_t sequence_id = 0;
  uint64_t offset = 0;
  uint64_t length = 0;
  uint64_t block_id = 0;
};

struct BlockMetaEntry {
  uint64_t block_id = 0;
  uint64_t file_offset = 0;
  uint64_t byte_size = 0;
  uint64_t first_seq_id = 0;
  uint64_t last_seq_id = 0;
};

class BlockDb {
 public:
  static BlockDb open(const std::string& dir) {
    BlockDb db;
    db.dir_ = dir;
    db.manifest_ = parse_manifest(read_file(join(dir, "manifest.json")));
    db.index_ = parse_index(join(dir, "sequences.index"));
    db.blocks_ = parse_blocks(join(dir, "blocks.meta"));
    if (db.blocks_.size() != db.manifest_.block_count) {
      throw std::runtime_error("block_count in manifest does not match blocks.meta");
    }
    if (db.index_.size() != db.manifest_.sequence_count) {
      throw std::runtime_error("sequence_count in manifest does not match sequences.index");
    }
    return db;
  }

  const BlockDbManifest& manifest() const { return manifest_; }
  const std::vector<SequenceIndexEntry>& index() const { return index_; }
  const std::vector<BlockMetaEntry>& blocks() const { return blocks_; }
  const std::string& dir() const { return dir_; }

  const BlockMetaEntry* block(uint64_t block_id) const {
    if (block_id >= blocks_.size()) {
      return nullptr;
    }
    return &blocks_[static_cast<std::size_t>(block_id)];
  }

  std::string data_path() const { return join(dir_, "sequences.data"); }

  uint64_t max_block_bytes() const {
    uint64_t max = 0;
    for (const BlockMetaEntry& block : blocks_) {
      if (block.byte_size > max) {
        max = block.byte_size;
      }
    }
    return max;
  }

  // Reads the raw bytes of one block (no alignment requirements; the io_uring
  // backend uses block()->file_offset/byte_size directly instead).
  std::string read_block(uint64_t block_id) const {
    const BlockMetaEntry* meta = block(block_id);
    if (meta == nullptr) {
      throw std::runtime_error("block id out of range");
    }
    std::ifstream in(data_path(), std::ios::binary);
    if (!in) {
      throw std::runtime_error("cannot open sequences.data");
    }
    in.seekg(static_cast<std::streamoff>(meta->file_offset));
    std::string buffer(meta->byte_size, '\0');
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (!in) {
      throw std::runtime_error("short read from sequences.data");
    }
    return buffer;
  }

 private:
  static std::string join(const std::string& dir, const std::string& name) {
    if (!dir.empty() && dir.back() == '/') {
      return dir + name;
    }
    return dir + "/" + name;
  }

  static std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      throw std::runtime_error("cannot open " + path);
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
  }

  static uint64_t find_uint(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) {
      throw std::runtime_error("manifest missing key: " + key);
    }
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) {
      throw std::runtime_error("malformed manifest for key: " + key);
    }
    ++pos;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\n' || text[pos] == '\r')) {
      ++pos;
    }
    uint64_t value = 0;
    bool any = false;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      value = value * 10 + static_cast<uint64_t>(text[pos] - '0');
      ++pos;
      any = true;
    }
    if (!any) {
      throw std::runtime_error("manifest key is not an unsigned integer: " + key);
    }
    return value;
  }

  static std::string find_string(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) {
      throw std::runtime_error("manifest missing key: " + key);
    }
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) {
      throw std::runtime_error("malformed manifest for key: " + key);
    }
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) {
      throw std::runtime_error("manifest value is not a string: " + key);
    }
    const std::size_t end = text.find('"', pos + 1);
    if (end == std::string::npos) {
      throw std::runtime_error("unterminated manifest string: " + key);
    }
    return text.substr(pos + 1, end - pos - 1);
  }

  static BlockDbManifest parse_manifest(const std::string& text) {
    BlockDbManifest manifest;
    manifest.format_version = find_uint(text, "format_version");
    manifest.database_id = find_uint(text, "database_id");
    manifest.version_major = find_uint(text, "major");
    manifest.version_minor = find_uint(text, "minor");
    manifest.sequence_count = find_uint(text, "sequence_count");
    manifest.block_count = find_uint(text, "block_count");
    manifest.block_size = find_uint(text, "block_size");
    manifest.total_bytes = find_uint(text, "total_bytes");
    manifest.checksum = find_string(text, "checksum");
    return manifest;
  }

  static std::vector<SequenceIndexEntry> parse_index(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
      throw std::runtime_error("cannot open " + path);
    }
    std::vector<SequenceIndexEntry> entries;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }
      std::istringstream fields(line);
      SequenceIndexEntry entry;
      if (!(fields >> entry.sequence_id >> entry.offset >> entry.length >> entry.block_id)) {
        throw std::runtime_error("malformed sequences.index line: " + line);
      }
      entries.push_back(entry);
    }
    return entries;
  }

  static std::vector<BlockMetaEntry> parse_blocks(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
      throw std::runtime_error("cannot open " + path);
    }
    std::vector<BlockMetaEntry> blocks;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }
      std::istringstream fields(line);
      BlockMetaEntry entry;
      if (!(fields >> entry.block_id >> entry.file_offset >> entry.byte_size >>
            entry.first_seq_id >> entry.last_seq_id)) {
        throw std::runtime_error("malformed blocks.meta line: " + line);
      }
      blocks.push_back(entry);
    }
    return blocks;
  }

  std::string dir_;
  BlockDbManifest manifest_;
  std::vector<SequenceIndexEntry> index_;
  std::vector<BlockMetaEntry> blocks_;
};

}  // namespace msaflow
