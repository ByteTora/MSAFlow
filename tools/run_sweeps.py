"""Run scheduler simulator sweeps over workloads, policies and DRAM budgets.

Deterministic replay: the simulator has no RNG, so one run per configuration.
No-coalesce baselines are only run at the reference DRAM fraction (0.25) to
bound runtime; they are the generic-baseline comparison for the Phase 1 gate.

Usage:
    python3 tools/run_sweeps.py [--sim build/simulator/msaflow-sim]
"""

import argparse
import json
import os
import subprocess
import sys

WORKLOADS = ("w1", "w2", "w3", "w4")
POLICIES = ("fifo", "lru", "sharing", "urgency", "msaflow-v0")
NC_POLICIES = ("fifo-nc", "lru-nc")
DRAM_FRACTIONS = (0.05, 0.25, 1.0)
REFERENCE_FRACTION = 0.25


def read_num_blocks(trace_path):
    with open(trace_path, "r", encoding="utf-8") as handle:
        return int(json.loads(handle.readline())["num_blocks"])


def run_one(sim, trace, out_dir, workload, policy, dram_blocks, args):
    nocoalesce = policy.endswith("-nc")
    base_policy = policy[:-3] if nocoalesce else policy
    out = os.path.join(out_dir, f"{workload}_{policy}_dram{args.dram_tag}.json")
    command = [
        sim,
        "--trace", trace,
        "--policy", base_policy,
        "--dram-blocks", str(dram_blocks),
        "--block-bytes", str(args.block_bytes),
        "--bandwidth-gbps", str(args.bandwidth_gbps),
        "--io-depth", str(args.io_depth),
        "--starvation-threshold-ms", str(args.starvation_threshold_ms),
        "--out", out,
    ]
    if nocoalesce:
        command.append("--no-coalesce")
    subprocess.run(command, check=True)
    return out


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim", default="build/simulator/msaflow-sim")
    parser.add_argument("--workloads-dir", default="workloads")
    parser.add_argument("--out", default="reports/scheduler")
    parser.add_argument("--block-bytes", type=int, default=134217728)
    parser.add_argument("--bandwidth-gbps", type=float, default=3.0)
    parser.add_argument("--io-depth", type=int, default=32)
    parser.add_argument("--starvation-threshold-ms", type=float, default=60000.0)
    args = parser.parse_args(argv)

    if not os.path.exists(args.sim):
        sys.exit(f"simulator not found: {args.sim}")
    os.makedirs(args.out, exist_ok=True)

    runs = 0
    for workload in WORKLOADS:
        trace = os.path.join(args.workloads_dir, f"{workload}.jsonl")
        num_blocks = read_num_blocks(trace)
        for fraction in DRAM_FRACTIONS:
            args.dram_tag = f"{fraction:g}"
            dram_blocks = max(1, round(fraction * num_blocks))
            policies = list(POLICIES)
            if fraction == REFERENCE_FRACTION:
                policies += list(NC_POLICIES)
            for policy in policies:
                run_one(args.sim, trace, args.out, workload, policy, dram_blocks, args)
                runs += 1
        print(f"{workload}: {runs} runs so far", flush=True)
    print(f"done: {runs} runs -> {args.out}")


if __name__ == "__main__":
    main()
