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
| Phase 2 — local NVMe runtime | done (2A portable core + 2B io_uring), on a Linux host |

Phase 2A added the portable runtime: FASTA → Block DB builder, `BufferPool`, a
synchronous `pread` backend, an extended metrics JSON, and a cross-check harness
that asserts the runtime reproduces the simulator's decisions. Phase 2B added a
liburing-based `IoUringBackend` with `O_DIRECT` and a storage benchmark. See
[`reports/phase2-storage.md`](reports/phase2-storage.md) and
[`benchmark_runs/`](benchmark_runs). Real-trace calibration (P0.4) and the HMMER/AF3
seams (Phases 3–4) remain open — no real AF3 database is available on the dev host.

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
| `core/` | C++20 schedule/cache/IO-core + `ReplayEngine` (shared by simulator and runtime) |
| `simulator/` | Discrete-event simulator; builds `msaflow-sim` |
| `runtime/` | Local storage runtime; builds `msaflow-runtime` (sim / pread / io_uring) |
| `storage/` | `StorageBackend` implementations: `sync/pread_backend`, `local_nvme/io_uring_backend` |
| `database/` | Block DB format + Python builder (`database/builder`) |
| `tools/` | Python offline tooling: traces, analysis, sweeps, synthetic data, benchmark |
| `tests/` | GoogleTest unit suite, golden fixtures, cross-check integration test |
| `benchmark_runs/` | Per-run metadata/results/report (spec Rule 4) |
| `docs/` | Architecture, reading notes, plans, entry criteria, toolchain, known issues |
| `reports/` | Phase reports and metrics JSON |
| `third_party/` | Pinned reference checkouts — see `third_party/REFERENCES.md` |
| `.agents/skills/` | Project-scoped coding-agent skills — see `docs/toolchain.md` |

## Build & test

Requires CMake ≥3.20, a C++20 compiler, Python 3.11+. The io_uring backend is
optional: build liburing under `third_party/liburing/_install` (see
`third_party/REFERENCES.md`) and CMake enables it automatically.

```bash
# build + tests (GoogleTest via FetchContent; unit + cross-check integration)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

# run the simulator
./build/simulator/msaflow-sim --trace workloads/w1.jsonl \
  --policy msaflow-v0 --dram-blocks 512 --block-bytes 134217728 \
  --bandwidth-gbps 3 --io-depth 32

# generate traces (w1..w4)
python3 -m tools.trace_generator.generate --workload w1 --seed 7 --out workloads/w1.jsonl

# build a Block Database and replay it through the runtime
python3 tools/make_synthetic_blockdb.py --out /tmp/benchdb \
  --total-bytes 268435456 --block-bytes 4194304 --seed 7
./build/runtime/msaflow-runtime --trace workloads/w1.jsonl --db /tmp/benchdb \
  --backend io_uring --direct --policy msaflow-v0 --dram-blocks 73 --io-depth 32

# Phase 2 storage benchmark
python3 tools/run_phase2_benchmark.py --db /tmp/benchdb --concurrency 8,16,32,64
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