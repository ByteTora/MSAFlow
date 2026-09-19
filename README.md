# MSAFlow

Query-aware, storage-aware data plane for high-throughput protein MSA search.

MSAFlow upgrades the Sequence Database from "a file each search process opens directly"
into a Runtime-managed Block Database, so multiple concurrent MSA queries cooperatively
consume database blocks: query cursor, cooperative scan, in-flight request coalescing,
shared/streaming DRAM cache, one-block prefetch, and async local NVMe I/O — without
modifying HMMER3 search semantics.

[中文文档](README.zh-CN.md) · [Spec](MSAFlow_Project_Execution_Spec_v1.0.md) · [Execution plan](docs/superpowers/plans/2026-09-18-msaflow-phase-0-2.md)

## Status

| Stage | State |
| --- | --- |
| Stage R — reference reconnaissance | done |
| Phase 0 — workload characterization | done (gate: PASS, conditional) |
| Phase 1 — scheduler simulator | done (gate: PASS) |
| Phase 2 — local NVMe runtime | gated on a Linux + NVMe host |

Phase 2 is blocked on environment, not on code readiness — the `StorageBackend`
seam (`core/include/msaflow/io_backend.hpp`) and entry criteria are already defined in
[`docs/phase2-entry-criteria.md`](docs/phase2-entry-criteria.md).

## Why

Existing systems have proven the pieces — ColabFold/MMseqs2 service-ized MSA, vLLM/LMCache
matured block/tiered caching, databases formalized Cooperative Scans — but none composes
them into one MSA storage runtime. MSAFlow's target KPIs are MSA latency ↓, throughput ↑,
physical bytes/query ↓, and coalescing ratio ↑, with correctness equal to baseline.

## Key finding so far

Under trace-replay, **scheduling policy does not matter**: the five policies read identical
or near-identical numbers of physical blocks. The gain comes from **single-flight coalescing**
(msaflow-v0 reads 4096 blocks vs 6240 for the no-coalesce baseline on W1, 1.52×) — not from
the I/O ordering formula. This supports the spec's Rule 5: do not add scheduler complexity in
V0. Real-world value still requires real-trace calibration (P0.4), deferred until a Linux +
real-database environment exists. See
[`reports/scheduler-baseline.md`](reports/scheduler-baseline.md) and
[`reports/workload-characterization.md`](reports/workload-characterization.md).

## Layout

| Path | Purpose |
| --- | --- |
| `core/` | C++20 schedule/cache/IO-core (shared by simulator and future runtime) |
| `simulator/` | Discrete-event simulator; builds `msaflow-sim` |
| `tools/` | Python offline tooling: trace generation, analysis, sweeps |
| `tests/` | GoogleTest unit suite + golden fixtures |
| `docs/` | Architecture, reading notes, plans, entry criteria, toolchain |
| `reports/` | Phase reports and metrics JSON |
| `third_party/` | Pinned reference checkouts — see `third_party/REFERENCES.md` |
| `.agents/skills/` | Project-scoped coding-agent skills — see `docs/toolchain.md` |

## Build & test

Requires CMake ≥3.20, a C++20 compiler, Python 3.11+.

```bash
# build + unit tests (GoogleTest fetched via FetchContent; 47 tests)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

# run the simulator
./build/simulator/msaflow-sim --trace workloads/w1.jsonl \
  --policy msaflow-v0 --dram-blocks 512 --block-bytes 134217728 \
  --bandwidth-gbps 3 --io-depth 32

# generate traces (w1..w4)
python3 -m tools.trace_generator.generate --workload w1 --seed 7 --out workloads/w1.jsonl

# analyze a trace
python3 -m tools.trace_analyzer.analyze --trace workloads/w1.jsonl --out reports/workload/w1.json

# scheduler sweeps + report
python3 tools/run_sweeps.py
python3 tools/report_scheduler.py
```

## Phases

| Phase | Scope | Status |
| --- | --- | --- |
| 0 | Workload characterization | done |
| 1 | Scheduler simulator | done |
| 2 | Local NVMe runtime (io_uring) | gated |
| 3 | JackHMMER adapter (reader seam only) | — |
| 4 | AlphaFold 3 integration | — |
| 5 | Scheduler V1 (lookahead, adaptive prefetch) | — |
| 6 | Remote storage (NVMe-oF / RDMA) | — |
| 7 | Distributed runtime | — |
| 8 | GPU / HBM | — |

## Documents

- [`docs/architecture-understanding.md`](docs/architecture-understanding.md) — the 9-question
  architecture report (objective, HMMER read path, AF3 pipeline, runtime data flow,
  lifecycle, io_uring path, risks, evidence gaps, assumptions).
- [`docs/repo-reading-notes.md`](docs/repo-reading-notes.md) — file:line evidence for every claim.
- [`docs/phase2-entry-criteria.md`](docs/phase2-entry-criteria.md) — Linux/NVMe checklist.
- [`docs/toolchain.md`](docs/toolchain.md) — installed coding-agent skills.

## License

[Apache-2.0](LICENSE). Reference checkouts under `third_party/` retain their own licenses;
no AlphaFold 3 weights or restricted assets are included.