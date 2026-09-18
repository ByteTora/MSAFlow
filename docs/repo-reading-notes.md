# Repository reading notes (Stage R / D1)

Pinned commits: `third_party/REFERENCES.md`. Date: 2026-09-18.
Method: 3 parallel recon agents over the pinned checkouts; every load-bearing claim below was spot-checked by hand against the same checkout (commands run on 2026-09-18).

---

## 1. AlphaFold 3 MSA execution path

### 1.1 Flow

- Config assembly from CLI: `run_alphafold.py:1003-1033`. Pipeline run per fold input: `run_alphafold.py:878`.
- Chains processed sequentially: `pipeline.py:594-607`.
- Protein MSA: 4 Jackhmmer tools run in parallel, `ThreadPoolExecutor(max_workers=4)` hard-coded: `pipeline.py:96-120` (comment at `:94-95`: sub-shelled, not GIL-bound).
- RNA MSA: 3 Nhmmer tools in a default executor: `pipeline.py:177-195`.
- Tool selection by config type: `msa.py:296-328`; DB path read at `msa.py:305` (Jackhmmer) and `:319` (Nhmmer).
- `_get_protein_msa_and_templates` / `_get_rna_msa` are `functools.cache`d (`pipeline.py:80`, `:165`) — config dataclasses are part of the cache key.

### 1.2 Jackhmmer tool

- `Jackhmmer.__init__`: `jackhmmer.py:44`; stores `self._database_path` at `:98`.
- Sharded detection `shards.get_sharded_paths(self._database_path)`: `jackhmmer.py:100` (`shards.py:86-103`).
- Sharded constraints: `n_iter==1`, non-None `z_value`, `max_sequences>1`, else `ValueError`: `jackhmmer.py:101-112`; shard parallelism `:114-118`; parallel fan-out `:171-174`; per-shard timings logged `:176-180`.
- Unsharded: single binary over whole DB `:183-195`; path passed as last CLI arg `:258`.
- Merge: `_merge_jackhmmer_results` `jackhmmer.py:181,284-344` (name→tblout map `:295-297`; lazy a3m parse `:300-308`; sort by e-value/bit/name `:310-317`; `heapq.merge` across shards `:320-323`; a3m truncated to `max_sequences` but full tblout kept `:325-337`).
- Flags: `--cpu` `:227`; `-N` `:228`; `-Z` `:245-246`; `--domZ` `:248-249`.
- Sharded-db caveat documented in-code: dedup across shards not detected; sharded may return more hits (bounded by shard count) `:60-66`.

### 1.3 Shard spec

- `prefix@N` parse regex: `shards.py:40-47`.
- On-disk naming `prefix-<i>-of-<N>` (5 digits) + Z-value requirement: `docs/performance.md:103-124`.
- `msa://...` does NOT match the shard regex — a URI must be resolved before `Jackhmmer` sees it.

### 1.4 Protein DB configs (all `n_iter=1`, `e_value=1e-4`)

| DB | max_sequences | z_value note | ref |
| --- | --- | --- | --- |
| uniref90 | 10,000 | from CLI | `pipeline.py:309-326` |
| mgnify | 5,000 | from CLI | `pipeline.py:327-344` |
| small_bfd | 5,000 | inline `138_515_945` matches paper | `pipeline.py:345-364` |
| uniprot_cluster_annot | 50,000 | from CLI | `pipeline.py:365-382` |

Defaults: `jackhmmer_n_cpu=8`, `jackhmmer_max_parallel_shards=None` (`pipeline.py:295-296`); CLI n_cpu = `min(cpu_count, 8)` (`run_alphafold.py:244-260`).

### 1.5 performance.md statements (quoted)

- Disk speed influences genetic search: `:74-75`.
- Recommends RAM-backed FS / more cores + parallelisation: `:77-79`.
- 4 DBs in parallel → optimal cores = per-Jackhmmer cores × 4: `:79-82`.
- Sharded DBs can *significantly* speed up on many-core + fast SSD/RAM-backed FS: `:87-91`.
- Z-value must be set for sharded DBs to scale e-values: `:118-124`.
- Concurrency example: (2 CPU) × (16 shards) × (4 DBs) = 128 cores per chain: `:150-152`.

### 1.6 Instrumentation

- Total proteiN-MSA wall time log only: `pipeline.py:125-129`; per-shard timings computed then discarded after logging: `jackhmmer.py:159-180`.
- No callback/structured trace. Capture without changing semantics = wrap `Jackhmmer.query` / `_query_shard_fn`, or parse logs.

### 1.7 Integration points (candidate)

- Single entry point for DB locator: `DatabaseConfig.path` (`msa_config.py:37-41`), consumed at `msa.py:305/:319`, `jackhmmer.py:100`, `:193`.
- Existing path-rewrite precedent: `replace_db_dir` (`run_alphafold.py:734-754`) — a URI resolver could slot at the same place (before config construction, `pipeline.py:312-315`).
- No runtime DB URI handling exists today.

---

## 2. HMMER target-DB read path and adapter seam

### 2.1 Three modes

Selected in `main()` (`jackhmmer.c:366`): MPI `:398-409`, else `serial_master` `:413`.

| mode | selector | target read call | line |
| --- | --- | --- | --- |
| serial | `ncpus==0` | `esl_sqio_Read(dbfp, dbsq)` | `jackhmmer.c:1609` |
| threaded | `--cpu N` (`:142`, `:644-645`) | `esl_sqio_ReadBlock(dbfp, block, -1, -1, FALSE, FALSE)` | `jackhmmer.c:1646` |
| MPI | `--mpi` (`:146`) | worker: `esl_sqfile_Position` + `esl_sqio_Read(dbfp, dbsq)` | `jackhmmer.c:1479-1482` |

Serial per-sequence consumption: `p7_pli_NewSeq` `:1611` → `p7_bg_SetLength` `:1612` → `p7_oprofile_ReconfigLength` `:1613` → `p7_Pipeline` `:1615` → `esl_sq_Reuse` `:1617` → `p7_pipeline_Reuse` `:1618`.
Threaded: `BLOCK_SIZE=1000` (`:173`), block allocated `esl_sq_CreateDigitalBlock` `:556`, consumed in `pipeline_thread` `:1673`, per-seq `dbsq = block->list + i` `:1703`, `p7_Pipeline` `:1709`, freed `esl_sq_DestroyBlock` `:766`.
Rewind: after each iteration `:712`, after each query `:743`; `esl_sqfile_IsRewindable` gate `:515`.

### 2.2 `next_block()` (MPI metadata blocks)

- `MAX_BLOCK_SIZE = 512*1024` bytes: `jackhmmer.c:868` (file-local define).
- `SEQ_BLOCK{offset,length,count}` `:870-874`; `BLOCK_LIST` `:876-882`.
- Accumulates via `esl_sqio_ReadInfo(sqfp,sq)` `:924`; block reused across queries when complete `:895-914`.
- Master sends descriptor only `:1207-1210`; worker still reads bytes via `Position`+`Read` `:1479-1482`.
- Conceptually: ordered block descriptor, but data path is still Easel reads.

### 2.3 Easel (cloned separately: `third_party/easel`)

- HMMER does not vendor Easel; README requires `git clone .../easel` (`third_party/hmmer/README.md:58`), and `configure.ac:64-67` includes `easel/m4/...`.
- `ESL_SQFILE` is a function-pointer vtable: `easel/esl_sqio.h:49-72` (`position`, `read`, `read_info`, `read_seq`, `read_block`, `is_rewindable`, ...).
- Format selection happens inside `sqfile_open` (`easel/esl_sqio.c:49`); `esl_sqfile_OpenDigital` `:261`; `esl_sqio_EncodeFormat` `:710`. No public API to inject a custom vtable: a custom reader means modifying Easel's format layer.
- `ESL_SQ_BLOCK` defined in `easel/esl_sq.h:147`; `esl_sqio_ReadBlock` prototype `esl_sqio.h:135`.

### 2.4 Seam recommendation (increasing scope)

1. **Serial-only minimal**: fill `dbsq` at `jackhmmer.c:1609` from MSAFlow, leave `:1611-1618` intact; honor rewinds `:712`, `:743`.
2. **All modes**: intercept exactly the four target primitives — `:1609` (serial), `:1482` (MPI), `:924` (`ReadInfo` inside `next_block`), `:1646` (`ReadBlock`) — and supply data or block descriptors. Query reads (`:571`, `:1089`, `:1438`) must stay on Easel.
3. **Easel format module**: cleanest long-term, but modifies Easel.

Contract MSAFlow must satisfy: produce a valid digital `ESL_SQ` (`abc = eslAMINO`, correct `dsq`/`n`; scoring reads only `sq->dsq`/`sq->n`, `p7_pipeline.c:713-722`), preserve input order, support replay/rewind from 0 across iterations and queries.

**Must NOT touch:** `p7_pipeline.c` (`:272`, `:576`, `:697`), `src/impl_sse|vmx|neon`, `p7_bg_SetLength`/`p7_oprofile_ReconfigLength` call sites, top-hits/threshold/alignment/output code, query read path.

---

## 3. liburing contract (for Phase 2 StorageBackend)

- Minimal lifecycle in `examples/io_uring-test.c:36-110`: `io_uring_queue_init` (`:36`) → `io_uring_get_sqe` (`:66`) → `io_uring_prep_readv` (`:69`) → `io_uring_submit` (`:76`) → `io_uring_wait_cqe` (`:89`) → `cqe->res` (`:97`) → `io_uring_cqe_seen` (`:102`) → `io_uring_queue_exit` (`:110`).
- Batch variant: `io_uring_submit_and_wait` (`examples/ucontext-cp.c:236`).
- Completion ↔ request association via `io_uring_sqe_set_data` / `io_uring_cqe_get_data` (`examples/io_uring-cp.c:77`, `:188`); `io_uring-test.c` instead relies on submission order.
- O_DIRECT: fd opened `O_RDONLY|O_DIRECT` (`io_uring-test.c:42`); buffer `posix_memalign(&buf, 4096, 4096)` (`:56`); iov_len 4096 (`:59`); offsets 4096-aligned (`:70`). IOPOLL requires O_DIRECT (`man/io_uring_setup_flags.7:30-38`). Registered buffers recommended with O_DIRECT (`man/io_uring_register_buffers.3:65-70`).
- Derived contract for `StorageBackend`: separate _prepare_ from _submit_; opaque per-request token (`user_data`); caller-provided 4K-aligned buffers; explicit drain-then-close. (Derived, not a source fact.)

---

## 4. vLLM / LMCache takeaways (design reference only)

- vLLM `block_pool.py`: central block list + free queue in eviction order (`:173-176`); refcount rules — writable iff `ref_cnt==1` and no hash (`:764-766`); hash→block map (`:33-131`); deferred free fenced on step sequence to protect in-flight consumers (`sched/scheduler.py:2633-2675`); `unpin` + `on_reuse` watcher = readable-until-reuse (`:702-721`).
  Transfer: central pool + refcount + free queue + readable-until-reuse. Do NOT transfer: GPU LIFO locality tricks, prefix-hash chaining, preempt-on-alloc-failure.
- LMCache: 4 tiers GPU/CPU-DRAM/local/remote (`architecture.rst:12-17`); plugin contract + loading order = priority (`storage_plugins.rst:9-31`, `:47-48`); lifecycle `pin`/`unpin`/`remove`/`close` (`v1/storage_backend/abstract_backend.py:195-264`); async batched submit with futures (`:72-101`).
  Transfer: lifecycle + tier shape. Do NOT transfer: torch/KV-specific allocator and chunking semantics.

---

## 5. Assumptions register

| # | Assumption | Evidence | Status |
| --- | --- | --- | --- |
| A1 | Every query scans the whole DB (all shards) per iteration | `pipeline.py:96-120`, `jackhmmer.py:171-174`, `:183-195` | verified |
| A2 | `n_iter=1` fixed for all protein DBs, no CLI override | `pipeline.py:317,335,353,373` | verified |
| A3 | Per-query parallelism = #shards (bounded by `max_parallel_shards`) | `jackhmmer.py:114-118,171-174` | verified |
| A4 | Target access order = sequential DB order within a shard | `jackhmmer.c:1609`, `:1646` | verified |
| A5 | DB file is fully re-scanned per query (rewind) | `jackhmmer.c:712,743` | verified |
| A6 | Threaded path reads blocks of up to 1000 sequences | `jackhmmer.c:173,556,1646` | verified |
| A7 | Adapter seam can be limited to jackhmmer.c target-read call sites | `easel/esl_sqio.h:49-72` (vtable internal, no injection API) | verified; exact level decided in Phase 3 |
| A8 | Real concurrency = queries × shards | `performance.md:150-152` (worked example, not a measurement) | partially verified |
| A9 | Logical block size 128 MiB (spec §39 CLI example) | spec only | model parameter, not fact |
| A10 | Arrival process and scan-rate skew distributions | none | model parameter; calibrate with real trace later (P0.4 deferred) |

## 6. Phase 0 parameter freeze (R4)

All values are **model parameters**, not measured facts (see A9/A10); sensitivity sweeps are part of P0.3.

| Parameter | Default | Basis |
| --- | --- | --- |
| `block_bytes` | 128 MiB | spec §39 CLI example |
| `num_blocks` | 4096 (512 GiB logical DB) | protein DB scale, synthetic |
| `num_shards` | 16 | spec §19 / performance.md:150-152 |
| shard mapping | contiguous block ranges | simplify; shards are sequential scanners |
| `n_iter` | 1 | A2 |
| concurrency levels | 8/16/32/64/128 queries | spec §21, §23 |
| timing model | per-stream `first_block_ns` + `block_interval_ns` (log-normal jitter) | Trace Format v1 |

## 7. Open questions / deferred

- P0.4 real-trace calibration deferred (needs Linux + real AF3 DBs). Phase 0 Gate must be reported with this limitation (spec R1).
- Exact Easel revision HMMER expects is not pinned by HMMER (separate repo, no submodule); `third_party/easel` commit recorded in `REFERENCES.md` as our pin.
- Real `esl_sqio_ReadBlock` sizing semantics for `max_residues=-1, max_sequences=-1` not examined (not needed before Phase 3).
