"""Build an MSAFlow Block Database from a FASTA file (spec §6).

Usage:
    python3 -m database.builder.build --fasta seqs.fa --out data/uniref90-msaflow \
        --block-bytes 1048576 --db-id 1
"""

import argparse
import sys

from database.builder import block_db


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build an MSAFlow Block Database")
    parser.add_argument("--fasta", required=True, help="input FASTA file")
    parser.add_argument("--out", required=True, help="output block DB directory")
    parser.add_argument("--block-bytes", type=int, default=134217728,
                        help="target block size in bytes (default 128 MiB)")
    parser.add_argument("--db-id", type=int, default=1)
    parser.add_argument("--version-major", type=int, default=1)
    parser.add_argument("--version-minor", type=int, default=0)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args(argv)

    with open(args.fasta, "r", encoding="utf-8") as handle:
        records = [
            block_db.SequenceRecord(sequence_id, residues)
            for sequence_id, (_, residues) in enumerate(block_db.parse_fasta(handle.read()))
        ]
    if not records:
        sys.exit(f"no sequences found in {args.fasta}")

    manifest = block_db.build_block_db(
        records,
        out_dir=args.out,
        target_block_bytes=args.block_bytes,
        db_id=args.db_id,
        version_major=args.version_major,
        version_minor=args.version_minor,
        overwrite=args.overwrite,
    )
    block_db.verify_block_db(args.out)
    print(
        f"built {args.out}: sequences={manifest['sequence_count']} "
        f"blocks={manifest['block_count']} bytes={manifest['total_bytes']} "
        f"checksum={manifest['checksum']}"
    )


if __name__ == "__main__":
    main()
