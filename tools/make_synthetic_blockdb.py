"""Build a synthetic Block Database directly (no FASTA intermediate).

Fast and deterministic: random bytes are generated with `random.Random(seed)`
and translated into the amino-acid alphabet, then sliced into fixed-length
sequences. Intended for Phase 2 storage benchmarks, where only the size and
block layout matter.

Usage:
    python3 tools/make_synthetic_blockdb.py --out /tmp/benchdb \
        --total-bytes 268435456 --seq-len 1000 --block-bytes 4194304 --seed 7
"""

import argparse
import os
import random
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from database.builder import block_db  # noqa: E402

RESIDUES = "ACDEFGHIKLMNPQRSTVWY"
_TABLE = bytes(ord(RESIDUES[i % len(RESIDUES)]) for i in range(256))


def build(out_dir, total_bytes, seq_len, block_bytes, seed, overwrite):
    rng = random.Random(seed)
    blob = rng.randbytes(total_bytes).translate(_TABLE)
    records = [
        block_db.SequenceRecord(i // seq_len, blob[i:i + seq_len].decode("ascii"))
        for i in range(0, len(blob), seq_len)
    ]
    manifest = block_db.build_block_db(
        records, out_dir, target_block_bytes=block_bytes, db_id=1,
        overwrite=overwrite)
    block_db.verify_block_db(out_dir)
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build a synthetic Block Database")
    parser.add_argument("--out", required=True)
    parser.add_argument("--total-bytes", type=int, default=268435456)
    parser.add_argument("--seq-len", type=int, default=1000)
    parser.add_argument("--block-bytes", type=int, default=4194304)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args(argv)

    manifest = build(args.out, args.total_bytes, args.seq_len, args.block_bytes,
                     args.seed, args.overwrite)
    print(
        f"built {args.out}: sequences={manifest['sequence_count']} "
        f"blocks={manifest['block_count']} bytes={manifest['total_bytes']}")


if __name__ == "__main__":
    main()
