"""Generate a synthetic amino-acid FASTA for Block DB / runtime testing.

Deterministic for a given seed; no external data needed. This is a stand-in for
real sequence databases (UniRef90 etc.), which are hundreds of GB and are not
available on the dev host.

Usage:
    python3 tools/make_synthetic_fasta.py --out /tmp/seqs.fa \
        --count 5000 --min-len 40 --max-len 600 --seed 7
"""

import argparse
import random

RESIDUES = "ACDEFGHIKLMNPQRSTVWY"


def generate(count, min_len, max_len, seed):
    rng = random.Random(seed)
    out = []
    for i in range(count):
        length = rng.randint(min_len, max_len)
        residues = "".join(rng.choice(RESIDUES) for _ in range(length))
        out.append((f"seq{i}", residues))
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate synthetic FASTA")
    parser.add_argument("--out", required=True)
    parser.add_argument("--count", type=int, default=5000)
    parser.add_argument("--min-len", type=int, default=40)
    parser.add_argument("--max-len", type=int, default=600)
    parser.add_argument("--seed", type=int, default=7)
    args = parser.parse_args(argv)

    records = generate(args.count, args.min_len, args.max_len, args.seed)
    total = 0
    with open(args.out, "w", encoding="utf-8") as handle:
        for name, residues in records:
            handle.write(f">{name}\n")
            for start in range(0, len(residues), 60):
                handle.write(residues[start:start + 60] + "\n")
            total += len(residues)
    print(f"wrote {len(records)} sequences ({total} residues) to {args.out}")


if __name__ == "__main__":
    main()
