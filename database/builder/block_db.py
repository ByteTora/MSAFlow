"""MSAFlow Block Database format (spec §6).

On-disk layout:

    <dir>/
    ├── manifest.json      # DB id/version, counts, block size, checksum
    ├── sequences.data     # raw residue bytes, concatenated in sequence order
    ├── sequences.index    # "sequence_id offset length block_id" per line
    └── blocks.meta        # "block_id file_offset byte_size first_seq_id last_seq_id"

Invariants (spec §6):
- a sequence never crosses a block boundary;
- blocks and DB versions are immutable;
- the DB-level checksum is verifiable;
- sequence id -> (offset, length, block) is traceable.

The `sequences.data` payload is raw bytes (ASCII residues) with no separators; the
index is the sole authority for boundaries.
"""

import hashlib
import json
import os

FORMAT_VERSION = 1
MANIFEST_NAME = "manifest.json"
DATA_NAME = "sequences.data"
INDEX_NAME = "sequences.index"
BLOCKS_NAME = "blocks.meta"
DEFAULT_ALIGNMENT = 4096

VALID_RESIDUES = set("ACDEFGHIKLMNPQRSTVWYBXZJUO*-.")


class BlockDbError(Exception):
    pass


class SequenceRecord:
    __slots__ = ("sequence_id", "residues")

    def __init__(self, sequence_id, residues):
        self.sequence_id = sequence_id
        self.residues = residues


def parse_fasta(text):
    """Yield (name, residues) from FASTA text. Residues are upper-cased and
    stripped of whitespace; blank lines are ignored."""
    name = None
    chunks = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith(">"):
            if name is not None:
                yield name, "".join(chunks)
            name = line[1:].strip()
            chunks = []
        else:
            chunks.append(line.upper())
    if name is not None:
        yield name, "".join(chunks)


def _pack_blocks(records, target_block_bytes):
    """Greedy packing that never splits a sequence across blocks.

    A sequence larger than the target occupies its own block (an oversized
    block); this is the only case where a block exceeds target_block_bytes.
    """
    if target_block_bytes <= 0:
        raise BlockDbError("target_block_bytes must be positive")

    blocks = []
    current = []
    current_bytes = 0
    for record in records:
        size = len(record.residues)
        if current and current_bytes + size > target_block_bytes:
            blocks.append(current)
            current = []
            current_bytes = 0
        current.append(record)
        current_bytes += size
        if current_bytes >= target_block_bytes:
            blocks.append(current)
            current = []
            current_bytes = 0
    if current:
        blocks.append(current)
    return blocks


def build_block_db(records, out_dir, target_block_bytes, db_id=1,
                   version_major=1, version_minor=0, overwrite=False,
                   data_alignment=DEFAULT_ALIGNMENT):
    """Write a Block DB from `records` and return the manifest dict.

    `sequences.data` is padded up to `data_alignment` bytes so O_DIRECT reads of
    the final block stay block-aligned. The padding is not part of the logical DB:
    `total_bytes` and `checksum` cover the logical (unpadded) content only.
    """
    if not overwrite and os.path.exists(os.path.join(out_dir, MANIFEST_NAME)):
        raise BlockDbError(f"output already looks like a block DB: {out_dir}")
    if data_alignment <= 0:
        data_alignment = 1

    records = list(records)
    for position, record in enumerate(records):
        if record.sequence_id != position:
            raise BlockDbError("sequence ids must be contiguous from 0")

    blocks = _pack_blocks(records, target_block_bytes)
    os.makedirs(out_dir, exist_ok=True)

    data = bytearray()
    index_lines = []
    blocks_meta_lines = []
    for block_id, block in enumerate(blocks):
        file_offset = len(data)
        first_seq_id = block[0].sequence_id
        last_seq_id = block[-1].sequence_id
        for record in block:
            payload = record.residues.encode("ascii")
            offset = len(data)
            data.extend(payload)
            index_lines.append(
                f"{record.sequence_id} {offset} {len(payload)} {block_id}")
        blocks_meta_lines.append(
            f"{block_id} {file_offset} {len(data) - file_offset} "
            f"{first_seq_id} {last_seq_id}")

    data_path = os.path.join(out_dir, DATA_NAME)
    logical_bytes = len(data)
    padded_bytes = -(-logical_bytes // data_alignment) * data_alignment
    with open(data_path, "wb") as handle:
        handle.write(data)
        if padded_bytes > logical_bytes:
            handle.write(b"\0" * (padded_bytes - logical_bytes))
    _write_lines(os.path.join(out_dir, INDEX_NAME), index_lines)
    _write_lines(os.path.join(out_dir, BLOCKS_NAME), blocks_meta_lines)

    checksum = hashlib.sha256(bytes(data)).hexdigest()
    manifest = {
        "format_version": FORMAT_VERSION,
        "database_id": int(db_id),
        "database_version": {"major": int(version_major), "minor": int(version_minor)},
        "sequence_count": len(records),
        "block_count": len(blocks),
        "block_size": int(target_block_bytes),
        "total_bytes": logical_bytes,
        "file_bytes": padded_bytes,
        "data_alignment": int(data_alignment),
        "checksum": f"sha256:{checksum}",
    }
    with open(os.path.join(out_dir, MANIFEST_NAME), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)
        handle.write("\n")
    return manifest


def _write_lines(path, lines):
    with open(path, "w", encoding="utf-8") as handle:
        for line in lines:
            handle.write(line + "\n")


def load_manifest(db_dir):
    with open(os.path.join(db_dir, MANIFEST_NAME), "r", encoding="utf-8") as handle:
        return json.load(handle)


def load_blocks(db_dir):
    """Return a list of block dicts from blocks.meta, in block_id order."""
    blocks = []
    with open(os.path.join(db_dir, BLOCKS_NAME), "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) != 5:
                raise BlockDbError(f"malformed blocks.meta line: {line!r}")
            block_id, file_offset, byte_size, first_seq_id, last_seq_id = map(int, fields)
            blocks.append({
                "block_id": block_id,
                "file_offset": file_offset,
                "byte_size": byte_size,
                "first_seq_id": first_seq_id,
                "last_seq_id": last_seq_id,
            })
    return blocks


def load_index(db_dir):
    """Return a list of sequence index dicts, in sequence_id order."""
    entries = []
    with open(os.path.join(db_dir, INDEX_NAME), "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) != 4:
                raise BlockDbError(f"malformed sequences.index line: {line!r}")
            sequence_id, offset, length, block_id = map(int, fields)
            entries.append({
                "sequence_id": sequence_id,
                "offset": offset,
                "length": length,
                "block_id": block_id,
            })
    return entries


def read_block_bytes(db_dir, block):
    with open(os.path.join(db_dir, DATA_NAME), "rb") as handle:
        handle.seek(block["file_offset"])
        return handle.read(block["byte_size"])


def verify_block_db(db_dir):
    """Check checksum, block/index consistency and sequence boundaries.

    Returns the manifest on success; raises BlockDbError otherwise.
    """
    manifest = load_manifest(db_dir)
    blocks = load_blocks(db_dir)
    index = load_index(db_dir)

    if len(blocks) != manifest["block_count"]:
        raise BlockDbError("block_count mismatch")
    if len(index) != manifest["sequence_count"]:
        raise BlockDbError("sequence_count mismatch")

    with open(os.path.join(db_dir, DATA_NAME), "rb") as handle:
        data = handle.read()
    logical_bytes = manifest["total_bytes"]
    expected_file_bytes = manifest.get("file_bytes", logical_bytes)
    if len(data) != expected_file_bytes:
        raise BlockDbError("file_bytes mismatch")
    if len(data) < logical_bytes:
        raise BlockDbError("data shorter than total_bytes")
    digest = hashlib.sha256(data[:logical_bytes]).hexdigest()
    if manifest["checksum"] != f"sha256:{digest}":
        raise BlockDbError("checksum mismatch")

    for expected_id, block in enumerate(blocks):
        if block["block_id"] != expected_id:
            raise BlockDbError("block ids not contiguous")
        if block["file_offset"] + block["byte_size"] > logical_bytes:
            raise BlockDbError(f"block {expected_id} extends past end of logical data")

    block_by_seq = {}
    for entry in index:
        if entry["offset"] + entry["length"] > logical_bytes:
            raise BlockDbError(f"sequence {entry['sequence_id']} extends past end of data")
        block = blocks[entry["block_id"]]
        if entry["offset"] < block["file_offset"]:
            raise BlockDbError(f"sequence {entry['sequence_id']} starts before its block")
        if entry["offset"] + entry["length"] > block["file_offset"] + block["byte_size"]:
            raise BlockDbError(f"sequence {entry['sequence_id']} crosses its block boundary")
        if not (block["first_seq_id"] <= entry["sequence_id"] <= block["last_seq_id"]):
            raise BlockDbError(f"sequence {entry['sequence_id']} outside block id range")
        block_by_seq[entry["sequence_id"]] = entry["block_id"]

    return manifest
