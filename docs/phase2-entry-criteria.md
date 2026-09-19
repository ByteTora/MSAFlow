# Phase 2 entry criteria (Local Storage Runtime)

Phase 2 is not started in this round. These are the gating conditions and the task
shape for when it does start. No Phase 2 code exists yet.

## Entry conditions (all three)

1. Phase 1 gate PASS — achieved (see `reports/scheduler-baseline.md`).
2. A Linux host with NVMe SSD is available (io_uring-capable kernel). The dev
   workstation is macOS — `core/` and `simulator/` are portable, but the
   `LocalNvmeBackend` and its benchmark require Linux.
3. Explicit user go-ahead to implement Phase 2.

## Environment checklist (record per benchmark, spec Rule 4)

- Linux kernel version (io_uring available/usable, `O_DIRECT` on the target FS)
- NVMe model + capacity; filesystem; mount options
- CPU model, core count, RAM
- compiler version (clang/gcc), build type (Release)
- liburing version/commit (pinned in `third_party/REFERENCES.md`)
- block size for the DB and DRAM buffer alignment (4K min)

## Task shape (to plan in detail at arrival)

### 2A — portable core (macOS-testable)

- C++20 runtime skeleton reusing `core/` (scheduler/cache/types) and the
  `StorageBackend` interface (`core/include/msaflow/io_backend.hpp`).
- FASTA → Block DB builder (`manifest.json`, `sequences.data`, `sequences.index`,
  `blocks.meta`; spec §6). Python prototype first, validated on a small FASTA.
- BufferPool with 4K-aligned `Buffer` (already in `core/include/msaflow/buffer.hpp`).
- A synchronous `pread` backend behind `StorageBackend` so the runtime is testable
  without io_uring; metrics JSON (reuse `metrics_to_json`).
- Cross-check harness: replay the same Trace Format v1 against the runtime and
  assert physical-read decisions match the simulator for the same policy.

### 2B — Linux NVMe (io_uring)

- `LocalNvmeBackend` over liburing: SQE prep → submit → CQE reap → callback, with
  `user_data` for completion association (liburing `io_uring-cp.c` pattern).
- Queue depth, drain-then-close, O_DIRECT-aligned reads.
- Benchmarks at 8/16/32/64/128 concurrent queries: page cache vs `O_DIRECT`/io_uring
  vs MSAFlow (spec §21). Record physical/logical reads, coalescing, hits, p50/p95/p99,
  throughput, CPU, DRAM.

## Validation boundary

Correctness baseline does not change in Phase 2: it is I/O-path work. Search-semantics
correctness gates live in Phase 3 (HMMER adapter) and Phase 4 (AF3 integration).