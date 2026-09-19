"""Regenerate the tiny Block DB fixture used by tests/unit/test_block_db.cpp.

Deterministic; run from the repository root:

    python3 tests/golden/make_blockdb_fixture.py
"""

import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO_ROOT)

from database.builder import block_db

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blockdb_tiny")

SEQUENCES = [
    "ACDEFGHIK",
    "LMNPQRST",
    "VWYACDE",
    "FGHIKLMNPQ",
    "RSTVWY",
]


def main():
    records = [
        block_db.SequenceRecord(i, residues) for i, residues in enumerate(SEQUENCES)
    ]
    manifest = block_db.build_block_db(
        records,
        out_dir=OUT_DIR,
        target_block_bytes=15,
        db_id=1,
        version_major=1,
        version_minor=0,
        overwrite=True,
    )
    block_db.verify_block_db(OUT_DIR)
    print(f"wrote {OUT_DIR}: blocks={manifest['block_count']}")


if __name__ == "__main__":
    main()
