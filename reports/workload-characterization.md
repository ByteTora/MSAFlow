# Phase 0 — Workload Characterization (D4)

Date: 2026-09-18. Generator: `tools/trace_generator` (Trace Format v1, seed 7).
Analyzer: `tools/trace_analyzer` → `reports/workload/{w1,w2,w3,w4}.json`.
Parameters: `docs/repo-reading-notes.md` §6 (4096 blocks × 128 MiB, 16 shards, 64 queries, `n_iter=1`).

Coalescing window base = `block_bytes / 3 GB/s` ≈ **44.7 ms** (×1/×2/×10).
DRAM column = LRU hit rate at 1% / 5% / 25% of the logical DB held in DRAM.

## Results

| workload | policy | logical reqs | unique blocks | sharing | consumers p50/p90/max | coalescible ×1/×2/×10 | DRAM LRU 1%/5%/25% | pair overlap |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| w1 | all_shared | 262,144 | 4,096 | 64.00 | 64/64/64 | 1.00 / 1.00 / 1.00 | 0.4% / 12.4% / **54.5%** | 1.000 |
| w2 | prefix_then_split | 163,840 | 4,096 | 40.00 | 64/64/64 | 0.98 / 1.00 / 1.00 | 1.9% / 9.1% / **39.4%** | 0.698 |
| w3 | random_windows | 26,240 | 3,788 | 6.93 | 7/11/13 | **0.22** / 0.39 / 0.83 | 1.6% / 7.0% / **35.1%** | 0.138 |
| w4 | disjoint_groups | 4,096 | 4,096 | 1.00 | 1/1/1 | 0.00 / 0.00 / 0.00 | 0% / 0% / 0% | 0.047 |

Raw: `reports/workload/*.json`.

## Findings

1. **Static overlap is not a useful signal.** Because AF3 issues one full-database scan per query (`docs/repo-reading-notes.md` A1/A2/A5), W1 sharing is exactly the query count (64) and W1/W2 consumer counts are saturated. Set intersection answers nothing; temporal metrics do.
2. **The coalescing window dominates the benefit.** W3 goes 22% → 83% as the window grows 44.7 ms → 447 ms. The exploitable fraction is set by how long a block stays INFLIGHT/READY relative to inter-consumer arrival gaps.
3. **DRAM reuse is the second lever.** At 25% DB capacity, W1 reaches 54.5% LRU hits and W3 still 35.1% — reuse distances are within one scan phase, not across full passes.
4. **W4 behaves as the designed control.** sharing = 1, coalescible = 0, DRAM reuse = 0 → the pipeline correctly identifies a zero-benefit workload. No false positives.

## Phase 0 gate

Spec question: *"under the target workload, how many potential consumers does each physical block have on average?"*
Answer: **1.0 (W4) to 64.0 (W1) consumers/block**; 6.9 for the low-overlap model (W3).

Decision (plan criteria: proceed if any workload shows coalescible fraction or DRAM reuse > 0; stop if all workloads have sharing ≈ 1 and coalescible < 5%):

> **PASS — proceed to Phase 1 (Scheduler Simulator).**

Conditions attached to this PASS:

1. W1/W2 overlap is **constructed by the generator's parameters**, not evidence that real AF3 workloads have locality (spec risk R1). This gate authorizes building the simulator; it does **not** assert the project's real-world benefit.
2. Phase 1 must report the low-overlap case (W3) separately from W1/W2, and must not claim value only from W1.
3. Real-trace calibration (P0.4) is deferred, not dropped. It must complete before Phase 2 outputs are treated as evidence.

## P0.4 — deferred real-trace calibration

- Not executed: no Linux + real AF3 database environment available (macOS-only workstation).
- Method when available: capture `(query_id, block_id, timestamp_ns)` at the native jackhmmer target-read seam (`jackhmmer.c:1609` serial, `:1646` threaded, `:1482` MPI — see notes §2.4), or parse existing per-shard timing logs (`jackhmmer.py:176-180`); emit Trace Format v1 so the same analyzer runs unchanged.
- Risk if skipped: if real coalescible fraction is well below W3's 0.22, the policy ranking from Phase 1 may not transfer. Phase 2 must re-run the analyzer on real traces.

## Reproduce

```bash
for w in w1 w2 w3 w4; do python3 -m tools.trace_generator.generate --workload $w --seed 7 --out workloads/$w.jsonl; done
for w in w1 w2 w3 w4; do python3 -m tools.trace_analyzer.analyze --trace workloads/$w.jsonl --out reports/workload/$w.json; done
python3 -m unittest discover -s tools -p "test_*.py" -t .
```
