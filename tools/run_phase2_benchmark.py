"""Phase 2 storage benchmark (spec §21).

Replays a synthetic all-shared workload (N concurrent queries each scanning the
whole Block Database) through msaflow-runtime for several backends and policies,
recording wall time and storage metrics. Results are written to
benchmark_runs/<timestamp>/{metadata.json,results.json,report.md} (spec Rule 4)
and a summary to reports/phase2-storage.md.

The dev host is an Alibaba Cloud EBS volume, not a physical NVMe device; treat
absolute numbers as indicative and compare backends relatively.

Usage:
    python3 tools/run_phase2_benchmark.py --db /tmp/benchdb \
        --concurrency 8,16,32,64 --out benchmark_runs
"""

import argparse
import datetime
import json
import os
import platform
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

BACKENDS = (
    ("pread", "pread", []),
    ("pread-direct", "pread", ["--direct"]),
    ("io_uring", "io_uring", []),
    ("io_uring-direct", "io_uring", ["--direct"]),
)
POLICIES = (
    ("msaflow-v0", []),
    ("msaflow-v0-nocoalesce", ["--no-coalesce"]),
)


def read_manifest(db_dir):
    with open(os.path.join(db_dir, "manifest.json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


def write_trace(path, num_blocks, num_queries, block_interval_ns=1000000):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(json.dumps({
            "type": "config", "format_version": 1, "db": "synth",
            "num_blocks": num_blocks, "num_shards": 1, "block_bytes": 0,
        }) + "\n")
        blocks = list(range(num_blocks))
        for query_id in range(num_queries):
            handle.write(json.dumps({
                "type": "query", "query_id": query_id, "db": "synth",
                "arrival_ns": 0,
                "streams": [{
                    "shard_id": 0, "first_block_ns": 0,
                    "block_interval_ns": block_interval_ns, "blocks": blocks,
                }],
            }) + "\n")


def collect_metadata(db_manifest, args):
    def run(cmd):
        try:
            return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.strip()
        except Exception:
            return "unknown"

    metadata = {
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "msaflow_commit": run(["git", "-C", REPO_ROOT, "rev-parse", "HEAD"]),
        "platform": platform.platform(),
        "kernel": platform.release(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "compiler": run(["g++", "--version"]).splitlines()[0] if shutil_which("g++") else "unknown",
        "cpu_model": next(
            (line.split(":", 1)[1].strip()
             for line in open("/proc/cpuinfo") if line.startswith("model name")), "unknown"),
        "cpu_count": os.cpu_count(),
        "mem_total_kb": next(
            (line.split(":", 1)[1].strip()
             for line in open("/proc/meminfo") if line.startswith("MemTotal")), "unknown"),
        "filesystem": run(["stat", "-f", "-c", "%T", os.path.dirname(db_manifest["_dir"])]),
        "db": {k: v for k, v in db_manifest.items() if k != "_dir"},
        "block_bytes": db_manifest["block_size"],
        "kernel_io_uring_config": run(["bash", "-lc",
            f"grep -i io_uring /boot/config-{platform.release()} 2>/dev/null || echo unknown"]),
        "liburing_version": "2.15 (built locally under third_party/liburing/_install)",
        "concurrency": args.concurrency,
    }
    return metadata


def shutil_which(name):
    from shutil import which
    return which(name)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--db", required=True)
    parser.add_argument("--runtime", default=os.path.join(REPO_ROOT, "build", "runtime", "msaflow-runtime"))
    parser.add_argument("--out", default=os.path.join(REPO_ROOT, "benchmark_runs"))
    parser.add_argument("--concurrency", default="8,16,32,64")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--dram-extra-blocks", type=int, default=8)
    parser.add_argument("--report", default=os.path.join(REPO_ROOT, "reports", "phase2-storage.md"))
    args = parser.parse_args(argv)

    args.concurrency = [int(c) for c in args.concurrency.split(",") if c]

    manifest = read_manifest(args.db)
    manifest["_dir"] = os.path.abspath(args.db)
    num_blocks = manifest["block_count"]
    block_bytes = manifest["block_size"]
    dram_blocks = num_blocks + args.dram_extra_blocks

    timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(args.out, timestamp)
    os.makedirs(out_dir, exist_ok=True)

    results = []
    for concurrency in args.concurrency:
        trace_path = os.path.join(out_dir, f"trace_c{concurrency}.jsonl")
        write_trace(trace_path, num_blocks, concurrency)
        for backend, backend_base, backend_flags in BACKENDS:
            for policy, policy_flags in POLICIES:
                command = [
                    args.runtime, "--trace", trace_path, "--db", args.db,
                    "--backend", backend_base, "--policy", "msaflow-v0",
                    "--dram-blocks", str(dram_blocks), "--io-depth", "32",
                    *backend_flags, *policy_flags,
                ]
                started = time.perf_counter()
                proc = subprocess.run(command, capture_output=True, text=True)
                wall_s = time.perf_counter() - started
                if proc.returncode != 0:
                    if "not compiled" in proc.stderr:
                        print(f"skip {backend} (not compiled)", flush=True)
                        break
                    print(f"FAILED c={concurrency} {backend} {policy}: {proc.stderr.strip()}",
                          file=sys.stderr)
                    continue
                payload = json.loads(proc.stdout)
                entry = {
                    "concurrency": concurrency,
                    "backend": backend,
                    "policy": policy,
                    "wall_s": wall_s,
                    "metrics": payload["metrics"],
                    "storage": payload["storage"],
                }
                results.append(entry)
                print(f"c={concurrency:3d} {backend:16s} {policy:22s} "
                      f"wall={wall_s:7.3f}s phys_reads={payload['metrics']['physical_block_reads']:5d} "
                      f"bytes={payload['storage']['physical_bytes_read']:12d}", flush=True)

    metadata = collect_metadata(manifest, args)
    metadata["dram_blocks"] = dram_blocks
    metadata["num_blocks"] = num_blocks
    with open(os.path.join(out_dir, "metadata.json"), "w", encoding="utf-8") as handle:
        json.dump(metadata, handle, indent=2, sort_keys=True)
    with open(os.path.join(out_dir, "results.json"), "w", encoding="utf-8") as handle:
        json.dump(results, handle, indent=2, sort_keys=True)

    report = render_report(metadata, results)
    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8") as handle:
        handle.write(report)
    if args.report:
        with open(args.report, "w", encoding="utf-8") as handle:
            handle.write(report)
    print(f"wrote {out_dir} and {args.report}")


def render_report(metadata, results):
    lines = [
        "# Phase 2 — local storage benchmark",
        "",
        f"Generated: {metadata['timestamp']}",
        f"MSAFlow commit: `{metadata['msaflow_commit']}`",
        f"Host: {metadata['platform']} / kernel {metadata['kernel']}",
        f"CPU: {metadata['cpu_model']} x{metadata['cpu_count']}; RAM {metadata['mem_total_kb']} kB",
        "Filesystem: **XFS on Alibaba Cloud EBS** (not physical NVMe) — indicative only.",
        "",
        f"DB: {metadata['num_blocks']} blocks x {metadata['block_bytes']} bytes "
        f"({metadata['db']['total_bytes']} bytes); DRAM budget {metadata['dram_blocks']} blocks; "
        f"io-depth 32. Workload: all-shared, N concurrent full-DB scans.",
        "",
        "## Wall time (s)",
        "",
    ]
    concurrencies = sorted({entry["concurrency"] for entry in results})
    backends = sorted({entry["backend"] for entry in results})
    policies = sorted({entry["policy"] for entry in results})
    for policy in policies:
        lines.append(f"### policy: {policy}")
        lines.append("")
        header = "| backend | " + " | ".join(f"c={c}" for c in concurrencies) + " |"
        lines.append(header)
        lines.append("|" + "---|" * (len(concurrencies) + 1))
        for backend in backends:
            cells = []
            for c in concurrencies:
                match = next((e for e in results if e["concurrency"] == c
                              and e["backend"] == backend and e["policy"] == policy), None)
                cells.append(f"{match['wall_s']:.3f}" if match else "")
            lines.append(f"| {backend} | " + " | ".join(cells) + " |")
        lines.append("")

    lines += ["## Physical reads / bytes", "",
              "| concurrency | backend | policy | physical_block_reads | physical_bytes_read | iops | avg_read_latency_ns |",
              "|---|---|---|---|---|---|---|"]
    for entry in sorted(results, key=lambda e: (e["concurrency"], e["backend"], e["policy"])):
        s = entry["storage"]
        m = entry["metrics"]
        lines.append(
            f"| {entry['concurrency']} | {entry['backend']} | {entry['policy']} | "
            f"{m['physical_block_reads']} | {s['physical_bytes_read']} | "
            f"{s['iops']:.1f} | {s['avg_read_latency_ns']} |")
    lines += [
        "",
        "## Notes",
        "",
        "- `pread` uses the Linux page cache; `pread-direct` and the `io_uring` variants use `O_DIRECT`.",
        "- With a DRAM budget larger than the DB, the cache retains shared blocks, so physical reads",
        "  converge to roughly the block count and the comparison is dominated by I/O path overhead.",
        "- Coalescing is only observable under simultaneous in-flight requests; `-nocoalesce` disables it.",
        "",
    ]
    return "\n".join(lines)


if __name__ == "__main__":
    main()
