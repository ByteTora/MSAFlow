"""Analyze a Trace Format v1 workload and emit metrics JSON.

Usage:
    python3 -m tools.trace_analyzer.analyze --trace workloads/w1.jsonl \
        --out reports/workload/w1.json
"""

import argparse
import json

from tools.trace_analyzer import metrics

DEFAULT_INTERVAL_NS = 1000000
DEFAULT_WINDOW_NS = 44739242  # 128 MiB / 3 GB/s


def read_trace(path, default_interval_ns=DEFAULT_INTERVAL_NS):
    config = {}
    queries = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if row.get("type") == "config":
                config = row
            elif "streams" in row:
                queries.append(row)
            elif "blocks" in row:
                queries.append(_flatten(row, default_interval_ns))
    return config, queries


def _flatten(row, default_interval_ns):
    arrival_ns = row.get("arrival_ns", 0)
    return {
        "type": "query",
        "query_id": row["query_id"],
        "db": row.get("db"),
        "arrival_ns": arrival_ns,
        "streams": [
            {
                "shard_id": 0,
                "first_block_ns": arrival_ns,
                "block_interval_ns": row.get("block_interval_ns", default_interval_ns),
                "blocks": row["blocks"],
            }
        ],
    }


def build_report(path, config, queries, bandwidth_gbps, multipliers, dram_fractions):
    events = metrics.expand_events(queries)
    base_window_ns = (
        round(config["block_bytes"] / bandwidth_gbps) if config.get("block_bytes") else DEFAULT_WINDOW_NS
    )
    windows_ns = [max(1, round(base_window_ns * factor)) for factor in multipliers]
    static = metrics.compute_static(events)
    temporal = metrics.compute_temporal(
        events, queries, windows_ns, dram_fractions, num_blocks=config.get("num_blocks")
    )
    return {
        "trace": path,
        "workload": config.get("workload"),
        "config": config,
        "windows_ns": windows_ns,
        "dram_fractions": dram_fractions,
        "metrics": {**static, **temporal},
    }


def _parse_floats(text):
    return [float(part) for part in text.split(",") if part]


def main(argv=None):
    parser = argparse.ArgumentParser(description="Analyze MSAFlow workload traces")
    parser.add_argument("--trace", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--bandwidth-gbps", type=float, default=3.0)
    parser.add_argument("--coalesce-multipliers", default="1,2,10")
    parser.add_argument("--dram-fractions", default="0.01,0.05,0.25")
    args = parser.parse_args(argv)

    multipliers = _parse_floats(args.coalesce_multipliers)
    dram_fractions = _parse_floats(args.dram_fractions)
    config, queries = read_trace(args.trace)
    report = build_report(args.trace, config, queries, args.bandwidth_gbps, multipliers, dram_fractions)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
        handle.write("\n")
    summary = report["metrics"]
    print(
        f"{args.trace}: queries={len(queries)} logical={summary['logical_block_requests']} "
        f"sharing={summary['sharing_factor']:.3f} "
        f"coalescible={list(summary['coalescible_fraction'].values())} -> {args.out}"
    )


if __name__ == "__main__":
    main()
