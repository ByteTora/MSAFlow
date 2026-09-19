import os
import shutil
import tempfile
import unittest

from database.builder import block_db


def make_records(lengths):
    return [
        block_db.SequenceRecord(i, "A" * length) for i, length in enumerate(lengths)
    ]


class BlockDbBuildTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="msaflow_blockdb_")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def build(self, lengths, target_block_bytes, **kwargs):
        records = make_records(lengths)
        out = os.path.join(self.tmp, "db")
        manifest = block_db.build_block_db(records, out, target_block_bytes, **kwargs)
        return out, manifest

    def test_sequence_never_crosses_a_block(self):
        out, _ = self.build([10, 10, 10, 10], target_block_bytes=25)
        index = block_db.load_index(out)
        blocks = block_db.load_blocks(out)
        for entry in index:
            block = blocks[entry["block_id"]]
            self.assertGreaterEqual(entry["offset"], block["file_offset"])
            self.assertLessEqual(
                entry["offset"] + entry["length"],
                block["file_offset"] + block["byte_size"],
            )

    def test_packing_uses_multiple_blocks(self):
        out, manifest = self.build([10, 10, 10, 10], target_block_bytes=25)
        self.assertEqual(manifest["block_count"], 2)
        self.assertEqual(manifest["sequence_count"], 4)

    def test_oversized_sequence_gets_its_own_block(self):
        out, manifest = self.build([100, 10], target_block_bytes=25)
        blocks = block_db.load_blocks(out)
        self.assertEqual(manifest["block_count"], 2)
        first = blocks[0]
        self.assertEqual(first["first_seq_id"], 0)
        self.assertEqual(first["last_seq_id"], 0)
        self.assertEqual(first["byte_size"], 100)

    def test_roundtrip_preserves_sequences(self):
        records = [
            block_db.SequenceRecord(0, "ACDEFGHIK"),
            block_db.SequenceRecord(1, "MNPQRSTVWY"),
            block_db.SequenceRecord(2, "ACDE"),
        ]
        out = os.path.join(self.tmp, "db")
        block_db.build_block_db(records, out, target_block_bytes=12)
        index = block_db.load_index(out)
        with open(os.path.join(out, block_db.DATA_NAME), "rb") as handle:
            data = handle.read()
        for entry, record in zip(index, records):
            payload = data[entry["offset"]:entry["offset"] + entry["length"]]
            self.assertEqual(payload.decode("ascii"), record.residues)

    def test_verify_detects_corruption(self):
        out, _ = self.build([10, 10], target_block_bytes=25)
        block_db.verify_block_db(out)
        with open(os.path.join(out, block_db.DATA_NAME), "r+b") as handle:
            handle.seek(0)
            handle.write(b"Z")
        with self.assertRaises(block_db.BlockDbError):
            block_db.verify_block_db(out)

    def test_refuses_to_overwrite_existing_db(self):
        out, _ = self.build([10], target_block_bytes=25)
        with self.assertRaises(block_db.BlockDbError):
            block_db.build_block_db(
                make_records([10]), out, target_block_bytes=25
            )


class FastaParseTest(unittest.TestCase):
    def test_multiline_and_lowercase(self):
        text = ">a\nacdefg\nhij\n>b\nMNPQ\n"
        self.assertEqual(
            list(block_db.parse_fasta(text)),
            [("a", "ACDEFGHIJ"), ("b", "MNPQ")],
        )


if __name__ == "__main__":
    unittest.main()
