# Phase 1 — Scheduler baseline (D5)

Deterministic replay simulator (`build/simulator/msaflow-sim`). `fifo-nc`/`lru-nc` = no single-flight coalescing (generic baseline). DRAM fractions are of the 4096-block logical DB; io-depth 32; block 128 MiB; 3 GB/s; starvation threshold 60 s.

### w1 — physical_block_reads (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 4096 | 4096 | 4096 |
| lru | 4096 | 4096 | 4096 |
| sharing | 4096 | 4096 | 4096 |
| urgency | 4096 | 4096 | 4096 |
| msaflow-v0 | 4096 | 4096 | 4096 |
| fifo-nc |  | 6240 |  |
| lru-nc |  | 6240 |  |

### w2 — physical_block_reads (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 4302 | 4096 | 4096 |
| lru | 4302 | 4096 | 4096 |
| sharing | 4320 | 4096 | 4096 |
| urgency | 4302 | 4096 | 4096 |
| msaflow-v0 | 4300 | 4096 | 4096 |
| fifo-nc |  | 5628 |  |
| lru-nc |  | 5628 |  |

### w3 — physical_block_reads (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 5627 | 4832 | 4035 |
| lru | 5627 | 4832 | 4035 |
| sharing | 5719 | 4911 | 3860 |
| urgency | 5627 | 4832 | 4035 |
| msaflow-v0 | 5627 | 4855 | 4033 |
| fifo-nc |  | 5267 |  |
| lru-nc |  | 5267 |  |

### w4 — physical_block_reads (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 4096 | 4096 | 4096 |
| lru | 4096 | 4096 | 4096 |
| sharing | 4096 | 4096 | 4096 |
| urgency | 4096 | 4096 | 4096 |
| msaflow-v0 | 4096 | 4096 | 4096 |
| fifo-nc |  | 4096 |  |
| lru-nc |  | 4096 |  |

### w1 — coalescing_ratio (ratio)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 0.984 | 0.984 | 0.984 |
| lru | 0.984 | 0.984 | 0.984 |
| sharing | 0.984 | 0.984 | 0.984 |
| urgency | 0.984 | 0.984 | 0.984 |
| msaflow-v0 | 0.984 | 0.984 | 0.984 |
| fifo-nc |  | 0.976 |  |
| lru-nc |  | 0.976 |  |

### w2 — coalescing_ratio (ratio)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 0.974 | 0.975 | 0.975 |
| lru | 0.974 | 0.975 | 0.975 |
| sharing | 0.974 | 0.975 | 0.975 |
| urgency | 0.974 | 0.975 | 0.975 |
| msaflow-v0 | 0.974 | 0.975 | 0.975 |
| fifo-nc |  | 0.966 |  |
| lru-nc |  | 0.966 |  |

### w3 — coalescing_ratio (ratio)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 0.786 | 0.816 | 0.846 |
| lru | 0.786 | 0.816 | 0.846 |
| sharing | 0.782 | 0.813 | 0.853 |
| urgency | 0.786 | 0.816 | 0.846 |
| msaflow-v0 | 0.786 | 0.815 | 0.846 |
| fifo-nc |  | 0.799 |  |
| lru-nc |  | 0.799 |  |

### w4 — coalescing_ratio (ratio)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 0 | 0 | 0 |
| lru | 0 | 0 | 0 |
| sharing | 0 | 0 | 0 |
| urgency | 0 | 0 | 0 |
| msaflow-v0 | 0 | 0 | 0 |
| fifo-nc |  | 0 |  |
| lru-nc |  | 0 |  |

### w1 — evictions (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 3936 | 3234 | 615 |
| lru | 3936 | 3234 | 615 |
| sharing | 3936 | 3234 | 615 |
| urgency | 3936 | 3234 | 615 |
| msaflow-v0 | 3936 | 3234 | 615 |
| fifo-nc |  | 3234 |  |
| lru-nc |  | 3234 |  |

### w2 — evictions (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 4128 | 3234 | 615 |
| lru | 4128 | 3234 | 615 |
| sharing | 4160 | 3234 | 615 |
| urgency | 4128 | 3234 | 615 |
| msaflow-v0 | 4128 | 3234 | 615 |
| fifo-nc |  | 3234 |  |
| lru-nc |  | 3234 |  |

### w3 — evictions (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 5376 | 3696 | 615 |
| lru | 5376 | 3696 | 615 |
| sharing | 5472 | 4004 | 615 |
| urgency | 5376 | 3696 | 615 |
| msaflow-v0 | 5376 | 3696 | 615 |
| fifo-nc |  | 3696 |  |
| lru-nc |  | 3696 |  |

### w4 — evictions (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 3936 | 3234 | 615 |
| lru | 3936 | 3234 | 615 |
| sharing | 3936 | 3234 | 615 |
| urgency | 3936 | 3234 | 615 |
| msaflow-v0 | 3936 | 3234 | 615 |
| fifo-nc |  | 3234 |  |
| lru-nc |  | 3234 |  |

### w1 — dram_occupancy_peak (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 184 | 921 | 3686 |
| lru | 184 | 921 | 3686 |
| sharing | 184 | 921 | 3686 |
| urgency | 184 | 921 | 3686 |
| msaflow-v0 | 184 | 921 | 3686 |
| fifo-nc |  | 921 |  |
| lru-nc |  | 921 |  |

### w2 — dram_occupancy_peak (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 184 | 921 | 3686 |
| lru | 184 | 921 | 3686 |
| sharing | 184 | 921 | 3686 |
| urgency | 184 | 921 | 3686 |
| msaflow-v0 | 184 | 921 | 3686 |
| fifo-nc |  | 921 |  |
| lru-nc |  | 921 |  |

### w3 — dram_occupancy_peak (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 184 | 921 | 3686 |
| lru | 184 | 921 | 3686 |
| sharing | 184 | 921 | 3686 |
| urgency | 184 | 921 | 3686 |
| msaflow-v0 | 184 | 921 | 3686 |
| fifo-nc |  | 921 |  |
| lru-nc |  | 921 |  |

### w4 — dram_occupancy_peak (blocks)

| policy | dram 0.05 | dram 0.25 | dram 1 |
|---|---|---|---|
| fifo | 184 | 921 | 3686 |
| lru | 184 | 921 | 3686 |
| sharing | 184 | 921 | 3686 |
| urgency | 184 | 921 | 3686 |
| msaflow-v0 | 184 | 921 | 3686 |
| fifo-nc |  | 921 |  |
| lru-nc |  | 921 |  |

## Phase 1 gate

- **w1** (dram 0.25): PASS
  - coalescing_msaflow > no-coalesce: yes
  - physical_msaflow < no-coalesce: yes
  - coalescing_msaflow >= lru: yes
  - physical_msaflow <= lru: yes
  - starvation == 0: yes
  - overhead < 1%: yes
- **w2** (dram 0.25): PASS
  - coalescing_msaflow > no-coalesce: yes
  - physical_msaflow < no-coalesce: yes
  - coalescing_msaflow >= lru: yes
  - physical_msaflow <= lru: yes
  - starvation == 0: yes
  - overhead < 1%: yes

**Gate overall: PASS**

## Findings

1. **Scheduling policy does not matter under replay.** On W1 the five coalescing 
   policies read 4096 blocks each (identical); on W2 they differ by at most 22.
   Phase-locked reuse plus immutable blocks saturate single-flight coalescing, so 
   I/O *ordering* adds nothing. This justifies not extending the scheduler (Rule 5):
   the V0 lever is coalescing, cache, and prefetch — not policy sophistication.
2. **The generic (no-coalesce) baseline is the real comparison.** At dram 0.25, 
   msaflow-v0 reads 4096 vs fifo-nc 6240 physical blocks on W1 (1.52x) and 4096 vs 
   5628 on W2 (1.37x). The gain is the single-flight invariant, not the score formula.
3. **Low-overlap (W3) benefit is modest** (4855 vs 5267, ~8%), matching the Phase 0 
   warning that the coalescing window dominates. Real value depends on real-trace 
   overlap (P0.4), not these synthetic distributions.
4. Scheduler overhead is negligible (decision cost ~0.1 us vs makespan seconds); 
   starvation is 0 at the 60 s threshold; DRAM-occupancy peak tracks the watermark.
