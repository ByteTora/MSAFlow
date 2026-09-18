"""Generate synthetic MSA review workload traces (Trace Format v1).

Usage:
    python3 -m tools.trace_generator.generate --workload w1 --seed 7 --out workloads/w1.jsonl
"""

import argparse
import json
import os
import random

from tools.trace_generator import workloads

CONFIG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "configs")
FORMAT_VERSION = 1


def load_config(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def generate_trace(config, seed):
    rng = random.Random(seed)
    header = {
        "type": "config",
        "format_version": FORMAT_VERSION,
        "workload": config.get("workload"),
        "db": config["db"],
        "num_blocks": config["num_blocks"],
        "num_shards": config["num_shards"],
        "block_bytes": config["block_bytes"],
        "seed": seed,
    }
    return [header] + workloads.build_query_specs(config, rng)


def write_trace(rows, path):
    with open(path, "w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, separators=(",", ":")) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Generate MSAFlow workload traces")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--workload", choices=["w1", "w2", "w3", "w4"])
    source.add_argument("--config", help="path to a config JSON")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--out", required=True, help="output JSONL path")
    args = parser.parse_args(argv)

    config_path = args.config or os.path.join(CONFIG_DIR, f"{args.workload}.json")
    config = load_config(config_path)
    rows = generate_trace(config, args.seed)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    write_trace(rows, args.out)
    print(f"wrote {len(rows) - 1} queries to {args.out} (seed={args.seed})")


if __name__ == "__main__":
    main()
