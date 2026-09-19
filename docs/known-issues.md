# Known issues and environment caveats

Recorded 2026-09-19 during Phase 2A work on the Alibaba Cloud Linux dev host.

## 1. Cross-platform tie-break nondeterminism (low impact)

The committed `reports/scheduler/*.json` were produced on macOS (libc++). Re-running
the identical sweep on Linux (libstdc++, GCC 10.2) reproduces **63/68 files
byte-for-byte**; 5 differ, all at the highest-pressure `dram 0.05` configurations:

| file | difference |
| --- | --- |
| `w2_msaflow-v0_dram0.05.json` | `shared_cache_hits` 6866 → 6888, p99 differs |
| `w3_fifo_dram0.05.json` | `physical_block_reads` 5627 → 5626 |
| `w3_lru_dram0.05.json` | same order of magnitude |
| `w3_msaflow-v0_dram0.05.json` | `physical_block_reads` 5627 → 5628 |
| `w3_urgency_dram0.05.json` | same order of magnitude |

Cause: `std::unordered_map` iteration order differs between libc++ and libstdc++,
changing tie-breaks in eviction victim selection and equal-priority ordering. Each
host is internally deterministic (running the same config twice is byte-identical);
only cross-platform comparison drifts. The Phase 1 gate verdict is unaffected
(W1/W2 key numbers match exactly). Comparison is safe only on generated data / source
SEMANTICS, not on the byte-exact text for these files.

## 2. Development host is not representative for Phase 2 performance

- `/data` is an **Alibaba Cloud Elastic Block Storage** volume (`nvme1n1`), not a
  physical local NVMe device. Latency/bandwidth numbers are indicative only.
- `/data` is ~96% full (~40 GB free), so only small synthetic Block Databases
  (a few GB) can be benchmarked locally. Real AF3 protein DBs (hundreds of GB) are
  not available on this host.
- Kernel is 5.10.134 with `CONFIG_IO_URING=y`; the older `kernel.io_uring_disabled`
  sysctl does not exist, and basic `IORING_OP_READ` works. IOPOLL / multishot are not
  assumed.

## 3. Phase 0 gate is conditional (unchanged)

W1/W2 high overlap is constructed by the trace generator, not measured from real AF3
workloads. Real-trace calibration (plan task P0.4) remains deferred because no real
database environment is available here. Per the Phase 0 report, the gate authorizes
building the simulator/runtime and does **not** assert real-world benefit.

## 4. Simulator buffer content is not modeled

`ReplayEngine` counts block reads and scheduling decisions; in simulator mode all
physical reads share one scratch buffer because block contents do not affect
decisions. The real runtime (`--backend pread`) allocates block-sized buffers
(`ReplayOptions::own_buffers`) so `O_DIRECT` reads land in correctly sized buffers.
