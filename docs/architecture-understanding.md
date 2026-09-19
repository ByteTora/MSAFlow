# MSAFlow Architecture Understanding Report

Consolidates Stage R evidence (`docs/repo-reading-notes.md`), which remains the source
of file:line detail. This document answers the nine questions the coding agent must
establish before further implementation.

## 1. Current objective

MSAFlow is a query-aware, storage-aware data plane for high-throughput protein MSA
search. V0 replaces "each JackHMMER process reads a Sequence Database file directly"
with a Runtime-managed Block Database. V0 scope is local NVMe + CPU DRAM only:
Query Cursor, Cooperative Scan, Inflight Coalescing, Shared/Streaming cache,
one-block prefetch, async I/O, HMMER adapter, AF3 integration, full metrics.
Success criterion: MSA latency ↓, throughput ↑, physical bytes/query ↓, coalescing
ratio ↑, with MSA correctness == baseline and single-query cost ≈ baseline
(spec §0, §3, §33, §35).

## 2. HMMER3 database read path

Three read modes selected in `jackhmmer.c` `main()`; all consume the target DB via
Easel `ESL_SQFILE`:

| mode | selector | target read | line |
| --- | --- | --- | --- |
| serial | `ncpus==0` | `esl_sqio_Read(dbfp, dbsq)` | `jackhmmer.c:1609` |
| threaded | `--cpu N` | `esl_sqio_ReadBlock(dbfp, block, -1, -1, FALSE, FALSE)` | `jackhmmer.c:1646` |
| MPI | `--mpi` | worker `esl_sqfile_Position` + `esl_sqio_Read` | `jackhmmer.c:1479-1482` |

Per-sequence handoff: `p7_pli_NewSeq` → `p7_bg_SetLength` → `p7_oprofile_ReconfigLength`
→ `p7_Pipeline` (scoring reads only `sq->dsq`/`sq->n`). Rewind per iteration `:712` and
per query `:743`.

**Seam**: the adapter must supply a valid digital `ESL_SQ` (alphabet `eslAMINO`) in
place of these target-read calls, preserving order and replay. Options (§2.4 of notes):
1. serial-only: fill `dbsq` at `:1609`. 2. all modes: intercept the three target
primitives. 3. Easel format module (cleanest, modifies Easel). `p7_pipeline.c` and
SIMD kernels are forbidden to touch.

## 3. AF3 MSA pipeline

- 4 protein Jackhmmer tools run in parallel (`ThreadPoolExecutor(max_workers=4)`,
  `pipeline.py:96-120`); per query each DB is a separate `Jackhmmer` invoked with `--cpu N`.
- All protein DB configs use `n_iter=1`, `e_value=1e-4`; `max_sequences` per DB
  (`pipeline.py:309-382`).
- Sharded DB (`prefix@N`): shard fan-out over threads (`jackhmmer.py:171-174`),
  merged by `_merge_jackhmmer_results` with `-Z`/`--domZ` e-value scaling.
- DB locator enters tools solely through `DatabaseConfig.path` (`msa_config.py:37-41`,
  read at `msa.py:305/:319`) — the single injection point for `msa://<db>/<version>`.
- Real concurrency = queries × shards (performance.md example: 2 CPU × 16 shards × 4 DB).

## 4. Runtime data flow

```
AF3 → MSAFlow Client → Query Manager → Scheduler → Block Manager → Cache Manager
                                                    → Storage API → {DRAM, Local NVMe}
                                                    → JackHMMER (sequence feed)
```
Search Plane ↔ Data Plane decoupled; logical DB ↔ physical storage decoupled; per-query
scan order unchanged; only inter-query physical I/O schedule changes; blocks immutable.

## 5. Query / Block / Cache / Inflight lifecycle

Types in `core/include/msaflow/types.hpp`. Lifecycle (replay model in `simulator/des.cpp`,
which Phase 2 runtime will mirror):

- Query: CREATED → RUNNABLE → (per block) WAITING/PROCESSING → DONE/CANCELLED/ERROR;
  tracks `current_block`/`next_block`.
- Block state: ABSENT → INFLIGHT (one physical read, invariant §5.5) → READY → IN_USE.
- Cache: SHARED (consumers>1 or future>0) vs STREAMING (single consumer, freed on
  release); eviction at HIGH=90% → LOW=75% watermark via `keep_score`
  (`2·log2(1+active) + 3·log2(1+future) + recency`).
- Inflight coalescing: requests to an INFLIGHT block attach (single-flight); the
  `--no-coalesce` flag disables this to form the generic baseline.

## 6. io_uring data path

`core/include/msaflow/io_backend.hpp` defines `StorageBackend` (`submit_read` + `poll`).
`simulator/simulated_backend.cpp` is the deterministic stand-in. Phase 2
`LocalNvmeBackend` reuses the same interface over liburing: queue init → SQE prep →
submit → CQE reap → `cqe->res`, with `O_DIRECT` (4K-aligned buffers/offsets) and opaque
`user_data` (liburing examples `io_uring-test.c`, `io_uring-cp.c`; notes §3).

## 7. Current design risks (spec §34)

- R1 low cross-query overlap → Phase 0 already flagged: synthetic W1/W2 overlap is
  constructed; real-trace validation (P0.4) deferred.
- R2 Linux page cache may already suffice → B1 baseline required.
- R3 JackHMMER I/O may not be the bottleneck → must quantify storage wait; don't claim
  a-priori speedup.
- R4 runtime overhead → C++ hot path, block-level ops (no per-sequence RPC).
- R5 remote storage complexity → local NVMe first.

## 8. Where evidence is still missing

- Real block-level locality under actual AF3 concurrency (no Linux + DB env yet).
  P0.4 capture method is defined but unexecuted.
- Whether storage wait is material vs JackHMMER compute on target DB sizes.
- `esl_sqio_ReadBlock` sizing semantics for `max_residues=-1, max_sequences=-1`
  (needed before Phase 3).
- EXact Easel revision HMMER expects (separate repo; pinned in `third_party/REFERENCES.md`).

## 9. Phase 1 assumptions to verify

See notes §5 (A1–A10). Load-bearing: every query scans the whole DB per iteration (A1);
`n_iter=1` fixed (A2); per-query parallelism = #shards (A3); sequential in-shard order (A4);
full re-scan per query (A5); threaded path reads ≤1000-seq blocks (A6). A9/A10 (128 MiB
block, arrival/skew distributions) are model parameters, not measured facts.