#include <gtest/gtest.h>

#include <string>

#include "msaflow/block_db.hpp"

#ifndef MSAFLOW_TEST_DATA_DIR
#error "MSAFLOW_TEST_DATA_DIR must be defined"
#endif

namespace msaflow {
namespace {

std::string fixture_dir() { return std::string(MSAFLOW_TEST_DATA_DIR) + "/blockdb_tiny"; }

TEST(BlockDb, LoadsManifest) {
  const BlockDb db = BlockDb::open(fixture_dir());
  const BlockDbManifest& manifest = db.manifest();
  EXPECT_EQ(manifest.format_version, 1u);
  EXPECT_EQ(manifest.database_id, 1u);
  EXPECT_EQ(manifest.version_major, 1u);
  EXPECT_EQ(manifest.version_minor, 0u);
  EXPECT_EQ(manifest.sequence_count, 5u);
  EXPECT_EQ(manifest.block_count, 4u);
  EXPECT_EQ(manifest.block_size, 15u);
  EXPECT_EQ(manifest.total_bytes, 40u);
  EXPECT_EQ(manifest.checksum,
            "sha256:3f58c67c36a585628d33015ae2cd0a227b34f8cbb0e7fcdfaa4c4b3de45c415d");
}

TEST(BlockDb, BlockMetaIsTraceable) {
  const BlockDb db = BlockDb::open(fixture_dir());
  ASSERT_NE(db.block(1), nullptr);
  const BlockMetaEntry& block = *db.block(1);
  EXPECT_EQ(block.file_offset, 9u);
  EXPECT_EQ(block.byte_size, 15u);
  EXPECT_EQ(block.first_seq_id, 1u);
  EXPECT_EQ(block.last_seq_id, 2u);
  EXPECT_EQ(db.block(99), nullptr);
}

TEST(BlockDb, ReadsRawBlockBytes) {
  const BlockDb db = BlockDb::open(fixture_dir());
  EXPECT_EQ(db.read_block(0), "ACDEFGHIK");
  EXPECT_EQ(db.read_block(1), "LMNPQRSTVWYACDE");
  EXPECT_EQ(db.read_block(3), "RSTVWY");
}

TEST(BlockDb, IndexContainsEverySequenceInOneBlock) {
  const BlockDb db = BlockDb::open(fixture_dir());
  ASSERT_EQ(db.index().size(), 5u);
  for (const SequenceIndexEntry& entry : db.index()) {
    const BlockMetaEntry* block = db.block(entry.block_id);
    ASSERT_NE(block, nullptr);
    EXPECT_GE(entry.offset, block->file_offset);
    EXPECT_LE(entry.offset + entry.length, block->file_offset + block->byte_size);
  }
}

TEST(BlockDb, MissingDirectoryThrows) {
  EXPECT_THROW(BlockDb::open(fixture_dir() + "_does_not_exist"), std::runtime_error);
}

}  // namespace
}  // namespace msaflow
