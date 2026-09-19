# Phase 2 — local storage benchmark

Generated: 2026-09-19T13:14:33.137583+00:00
MSAFlow commit: `3a9dced2050518fca7c6b11e349d443c52335a60`
Host: Linux-5.10.134-19.101.al8.x86_64-x86_64-with-glibc2.32 / kernel 5.10.134-19.101.al8.x86_64
CPU: Intel(R) Xeon(R) Platinum 8469C x32; RAM 192825992 kB kB
Filesystem: **XFS on Alibaba Cloud EBS** (not physical NVMe) — indicative only.

DB: 65 blocks x 4194304 bytes (268435456 bytes); DRAM budget 73 blocks; io-depth 32. Workload: all-shared, N concurrent full-DB scans.

## Wall time (s)

### policy: msaflow-v0

| backend | c=8 | c=16 | c=32 | c=64 |
|---|---|---|---|---|
| io_uring | 0.268 | 0.262 | 0.265 | 0.283 |
| io_uring-direct | 1.359 | 1.190 | 1.113 | 1.124 |
| pread | 0.263 | 0.266 | 0.287 | 0.275 |
| pread-direct | 1.111 | 1.195 | 1.113 | 1.130 |

### policy: msaflow-v0-nocoalesce

| backend | c=8 | c=16 | c=32 | c=64 |
|---|---|---|---|---|
| io_uring | 0.421 | 0.587 | 0.917 | 1.602 |
| io_uring-direct | 16.404 | 32.810 | 65.632 | 65.635 |
| pread | 0.422 | 0.586 | 0.909 | 1.605 |
| pread-direct | 16.417 | 32.826 | 65.637 | 131.278 |

## Physical reads / bytes

| concurrency | backend | policy | physical_block_reads | physical_bytes_read | iops | avg_read_latency_ns |
|---|---|---|---|---|---|---|
| 8 | io_uring | msaflow-v0 | 65 | 268435456 | 502.0 | 250866 |
| 8 | io_uring | msaflow-v0-nocoalesce | 520 | 2147483648 | 1812.1 | 1699321 |
| 8 | io_uring-direct | msaflow-v0 | 65 | 268435456 | 53.1 | 18685432 |
| 8 | io_uring-direct | msaflow-v0-nocoalesce | 520 | 2147483648 | 32.0 | 202924502 |
| 8 | pread | msaflow-v0 | 65 | 268435456 | 502.3 | 249747 |
| 8 | pread | msaflow-v0-nocoalesce | 520 | 2147483648 | 1813.3 | 1693437 |
| 8 | pread-direct | msaflow-v0 | 65 | 268435456 | 66.6 | 1879270 |
| 8 | pread-direct | msaflow-v0-nocoalesce | 520 | 2147483648 | 31.9 | 140267468 |
| 16 | io_uring | msaflow-v0 | 65 | 268435456 | 507.4 | 126080 |
| 16 | io_uring | msaflow-v0-nocoalesce | 1040 | 4294967296 | 2302.2 | 2848656 |
| 16 | io_uring-direct | msaflow-v0 | 65 | 268435456 | 61.5 | 16105446 |
| 16 | io_uring-direct | msaflow-v0-nocoalesce | 1040 | 4294967296 | 31.8 | 349362046 |
| 16 | pread | msaflow-v0 | 65 | 268435456 | 502.6 | 126623 |
| 16 | pread | msaflow-v0-nocoalesce | 1040 | 4294967296 | 2305.4 | 2827615 |
| 16 | pread-direct | msaflow-v0 | 65 | 268435456 | 61.4 | 1020912 |
| 16 | pread-direct | msaflow-v0-nocoalesce | 1040 | 4294967296 | 31.8 | 267958780 |
| 32 | io_uring | msaflow-v0 | 65 | 268435456 | 507.3 | 66543 |
| 32 | io_uring | msaflow-v0-nocoalesce | 2080 | 8589934592 | 2651.7 | 5350514 |
| 32 | io_uring-direct | msaflow-v0 | 65 | 268435456 | 66.3 | 14930400 |
| 32 | io_uring-direct | msaflow-v0-nocoalesce | 2080 | 8589934592 | 31.8 | 577679739 |
| 32 | pread | msaflow-v0 | 65 | 268435456 | 505.4 | 66324 |
| 32 | pread | msaflow-v0-nocoalesce | 2080 | 8589934592 | 2684.8 | 5267983 |
| 32 | pread-direct | msaflow-v0 | 65 | 268435456 | 66.4 | 475021 |
| 32 | pread-direct | msaflow-v0-nocoalesce | 2080 | 8589934592 | 31.8 | 520229497 |
| 64 | io_uring | msaflow-v0 | 65 | 268435456 | 459.5 | 43572 |
| 64 | io_uring | msaflow-v0-nocoalesce | 4160 | 17179869184 | 2852.5 | 10387434 |
| 64 | io_uring-direct | msaflow-v0 | 65 | 268435456 | 66.1 | 14948455 |
| 64 | io_uring-direct | msaflow-v0-nocoalesce | 4160 | 8589934592 | 63.5 | 292894530 |
| 64 | pread | msaflow-v0 | 65 | 268435456 | 490.0 | 40685 |
| 64 | pread | msaflow-v0-nocoalesce | 4160 | 17179869184 | 2838.7 | 10539630 |
| 64 | pread-direct | msaflow-v0 | 65 | 268435456 | 66.1 | 245150 |
| 64 | pread-direct | msaflow-v0-nocoalesce | 4160 | 17179869184 | 31.7 | 1025415599 |

## Notes

- `pread` uses the Linux page cache; `pread-direct` and the `io_uring` variants use `O_DIRECT`.
- With a DRAM budget larger than the DB, the cache retains shared blocks, so physical reads
  converge to roughly the block count and the comparison is dominated by I/O path overhead.
- Coalescing is only observable under simultaneous in-flight requests; `-nocoalesce` disables it.
