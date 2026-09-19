# MSAFlow Stage R + Phase 0–2 执行计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Before writing any code, read the `karpathy-guidelines` skill.**

**Goal:** 按 spec 顺序完成参考代码精读（Stage R）、Phase 0 workload characterization、Phase 1 scheduler simulator，并为 Phase 2 留下可就绪的入口。

**Architecture:** 热路径全部 C++20（`core/` 逻辑层 + `simulator/` 离散事件仿真层，共用同一套 scheduler/cache/inflight 实现）；Python 只做离线 tooling（trace 生成、分析、报告）。Phase 2 的 io_uring backend 复用同一 `StorageBackend` 接口。

**Tech Stack:** C++20 / CMake ≥3.20 / GoogleTest（FetchContent 固定版本）；Python 3.11+ 标准库（离线工具，不引第三方依赖）。

**Spec:** `MSAFlow_Project_Execution_Spec_v1.0.md`

## Status (2026-09-19)

Executed: Stage R (R1–R4), Phase 0 (P0.1–P0.3, P0.5), Phase 1 (P1.1–P1.7) — all tasks below
complete and committed; Phase 0 gate = PASS (conditional), Phase 1 gate = PASS.
P0.4 (real-trace calibration) deferred: needs Linux + real databases.

Not started: Phase 2. Entry criteria and task shape live in
`docs/phase2-entry-criteria.md`; the simulator `StorageBackend` seam is `core/include/msaflow/io_backend.hpp`.

Deviations from this plan, recorded honestly:

- Phase 1 simulator is C++20 (shared `core/`), not Python — single implementation reused by Phase 2.
- `policy` is a scheduling policy; a separate `--no-coalesce` flag supplies the generic baseline
  (the gate's real comparison), since replay makes the five policies near-identical.
- Sweeps are deterministic (no RNG), so the planned seed dimension was dropped; `-nc` baselines
  run only at the 0.25 DRAM reference to bound runtime.
- `SimOptions` priority defaults are scale-dependent (`sharing_weight=1e6` for ns-domain inputs);
  tunable, not a project conclusion (spec §7.3).
- `drain` in `simulator/des.cpp` re-submits pending work each iteration; an early version omitted
  this and could loop — fixed and covered by `Des.DrainsPendingBacklogForDisjointBlocks`.


## Global Constraints

- **禁止提前实现**（spec §3.2 / Rule 5）：GPU-HMMER、MMseqs2-GPU、HBM、GDS、RDMA、NVMe-oF、分布式调度、ML scheduler、HMMER 分数/filter kernel 改动。Phase 0/1 内只允许 spec §20 列出的策略。
- **正确性不可破坏**（spec §33）：不允许 approximate search、sequence filtering、early top-K、改阈值/E-value/DB 内容、静默丢 block。
- **Phase 0 Gate（Rule 6）**：若 block overlap ≈ 0 / 无可利用时间复用 → 停止，不堆 cache/scheduler 功能，重新评估项目价值。
- **每完成一个 Phase 必须 commit**（Rule 3）；benchmark 必须落 `benchmark_runs/<ts>/{metadata.json,results.json,report.md}`（Rule 4）。
- **karpathy-guidelines**（每条任务都适用）：
  1. Think before coding：先读源码，假设必须显式列出并验证。
  2. Simplicity first：不为单次用途造抽象；允许的接口只有 `StorageBackend`（两个实现：sim / io_uring）与 policy（五个实现）。不上模板框架/DI/配置系统。单文件超 ~300 行视为拆分信号。
  3. Surgical changes：每任务只碰自己 Files 列出的文件；`third_party/` 只读；HMMER 只碰 `esl_sqio` seam。
  4. Goal-driven：每个任务 = 失败测试 → 最小实现 → 测试通过 → commit；不接受无 verify 命令的任务。
- 本机环境：macOS 15.6 / arm64 / clang 16 / CMake 3.31 / Python 3.11.9。io_uring 相关代码仅定义接口，Phase 2 在 Linux 实现。
- 参考仓 pin 规则：LMCache 文档在 `dev` 分支（`main` 无此路径）；`vLLM`/`LMCache` 用 `--filter=blob:none` 克隆。

## Trace Format v1（冻结）

JSONL，逐行一个 JSON 对象。第一行为可选 config 行，随后每行一个 query。

```json
{"type":"config","format_version":1,"db":"uniref90","num_blocks":8192,"num_shards":16,"block_bytes":134217728}
{"type":"query","query_id":1,"db":"uniref90","arrival_ns":0,
 "streams":[
   {"shard_id":0,"first_block_ns":0,"block_interval_ns":100000,"blocks":[0,1,2]},
   {"shard_id":1,"first_block_ns":0,"block_interval_ns":120000,"blocks":[512,513]}
 ]}
```

- `streams` 对应 AF3 每 query 并行扫多个 shard 的真实形态；block 访问时刻 = `first_block_ns + i * block_interval_ns`。
- 兼容：无 `type` 且含顶层 `blocks` 的行按 spec §16.1 flat 形式解析（单 stream、shard_id=0、interval 取 CLI 默认）。
- 生成器输出始终带计时字段，保证 analyzer 与 simulator 对同一 trace 得到一致时间线。

## File Structure

```
CMakeLists.txt                     # P1.1
core/
  include/msaflow/types.hpp        # P1.1 §5 数据结构
  include/msaflow/query_registry.hpp / block_aggregator.hpp / priority_policy.hpp   # P1.2
  include/msaflow/cache.hpp        # P1.3
  include/msaflow/buffer.hpp / io_backend.hpp   # P1.4
  include/msaflow/metrics.hpp      # P1.5
  src/*.cpp
simulator/
  trace_reader.{hpp,cpp}           # P1.5
  simulated_backend.{hpp,cpp}      # P1.4
  des.{hpp,cpp}                    # P1.5
  cli.cpp                          # P1.5 → build/msaflow-sim
tools/
  trace_generator/{generate.py,workloads.py,configs/w1..w4.json}   # P0.1
  trace_analyzer/{analyze.py,metrics.py}                            # P0.2
  run_sweeps.py / report_scheduler.py                               # P1.7
tests/
  unit/*.cpp                       # P1.1–P1.6
  golden/*                         # P1.6
docs/repo-reading-notes.md         # R3
docs/phase2-entry-criteria.md      # Phase 2 入口（只写文档）
workloads/*.jsonl                  # 生成物，gitignore
reports/workload/*.json            # P0.3
reports/scheduler/*.json           # P1.7
```

---

## Stage R — Repo init + Reference recon

### Task R1: Repository init

**Files:** Create `.gitignore`, `README.md`, `LICENSE`, `docs/superpowers/plans/2026-09-18-msaflow-phase-0-2.md`

- [x] **Step 1:** `git init -b main`；建目录骨架（docs/ third_party/ tools/ workloads/ reports/ benchmark_runs/）
- [x] **Step 2:** 写 `.gitignore`（build/、third_party/* 除 REFERENCES.md、data/、workloads/*.jsonl、__pycache__、.venv）
- [x] **Step 3:** 写 `README.md`、取 Apache-2.0 `LICENSE`
- [x] **Step 4:** 写本 plan 文档
- [ ] **Step 5:** `git add -A && git commit -m "chore: initialize msaflow repo with execution plan"`

**Verify:** `git log --oneline` 出现该 commit；`git status` 干净。

### Task R2: Clone 参考仓 + pin commit

**Files:** Create `third_party/REFERENCES.md`

- [ ] **Step 1:** clone（顺序执行，大仓用 partial clone）：

```bash
git clone https://github.com/google-deepmind/alphafold3.git third_party/alphafold3
git clone https://github.com/EddyRivasLab/hmmer.git third_party/hmmer
git clone https://github.com/axboe/liburing.git third_party/liburing
git clone --filter=blob:none https://github.com/vllm-project/vllm.git third_party/vllm
git clone --filter=blob:none https://github.com/LMCache/LMCache.git third_party/LMCache
git -C third_party/LMCache checkout dev
```

- [ ] **Step 2:** 对每个 repo `git rev-parse HEAD` 与 `git rev-parse --abbrev-ref HEAD`，写入 `REFERENCES.md` 表格（Project / URL / branch / commit / 用途 / clone date）。
- [ ] **Step 3:** commit：`chore(third_party): pin reference repositories`

**Verify:** `REFERENCES.md` 5 行 commit hash 与 `rev-parse` 输出一致；`git status --porcelain third_party` 为空（除 REFERENCES.md 外全被 ignore）。

### Task R3: 精读 priority-1 源码 → repo-reading-notes.md

**Files:** Create `docs/repo-reading-notes.md`

**Interfaces:** 本任务产出的“seam 清单”是 Task P1.4（StorageBackend 接口）与未来 Phase 3 的唯一依据。

- [ ] **Step 1:** 并行派 3 个 explorer 子代理读：
  - (a) AF3：`docs/performance.md`、`src/alphafold3/data/pipeline.py`、`msa_config.py`、`tools/jackhmmer.py`、`tools/shards.py`、`run_alphafold.py`
  - (b) HMMER：`src/jackhmmer.c`（重点 `esl_sqio_Read` / `esl_sqio_ReadBlock` / `next_block` 调用点与循环）、`src/p7_pipeline.c`（sequence 消费入口）、`easel/esl_sqio.*` 接口
  - (c) liburing `examples/`（io_uring 提交/收割模式、O_DIRECT 对齐要求）、vLLM `vllm/v1/core/block_pool.py` + `vllm/v1/core/sched/scheduler.py`（ref-count / eviction 对象模型）、LMCache `docs/source/developer_guide/extending_lmcache/storage_plugins.rst`（tier 插件接口）
- [ ] **Step 2:** 合并为 `docs/repo-reading-notes.md`，必须含：
  - AF3 MSA 执行路径（每 DB 一个 tool、sharded 时 shard fan-out 并行、n_iter=1 约束、Z/domZ、`_merge_jackhmmer_results`）
  - HMMER target DB 读取路径函数级链路 + **集成 seam 清单**（替换 Easel `ESL_SQFILE` 读层；`p7_pipeline.c` 禁改）
  - AF3 配置抽象点（`DatabaseConfig.path` → `msa://<db>/<version>` 候选）
  - vLLM/LMCache 可借鉴对象模型（ref-count、block pool、storage plugin），注明“参考不复制”
  - **假设清单**（表格：假设 / 验证来源 file:line / 状态）
- [ ] **Step 3:** 我本人抽查每条假设的关键 file:line，删掉无证据条目或降级为 open question。
- [ ] **Step 4:** commit：`docs: add reference repo reading notes`

**Verify:** notes 能回答三问：块级读经过哪个函数？谁决定下一个读哪个 sequence？page cache 命中发生在哪一层？

### Task R4: 冻结 Phase 0 参数

**Files:** Modify `docs/repo-reading-notes.md`（追加一节 `Phase 0 parameter freeze`）

- [ ] **Step 1:** 依据 R3 证据确定：`num_blocks`、`num_shards`、`block_bytes`、默认并发数、`n_iter` 语义（1 次全库扫描/query）。
- [ ] **Step 2:** 写入 notes 并 commit：`docs: freeze phase-0 workload parameters`

**Verify:** 参数逐项标注来源（spec 条款或源码 file:line），无“拍脑袋”值。

---

## Phase 0 — Workload Characterization

### Task P0.1: Trace generator

**Files:** Create `tools/trace_generator/{__init__.py,generate.py,workloads.py}`, `tools/trace_generator/configs/{w1,w2,w3,w4}.json`, `tools/trace_generator/tests/{__init__.py,test_generate.py}`

**Interfaces:**
- `generate_trace(config: dict, seed: int) -> list[dict]`（返回 trace 行，首行 config）
- CLI：`python3 -m tools.trace_generator.generate --workload w1 --seed 7 --out workloads/w1.jsonl`
- workload 语义（对照 spec §16.2）：
  - W1 high-overlap：全 query 全 shard、相同速率、到达密集（相位锁定）
  - W2 medium-overlap：公共 shard 前缀后按组分叉到不同 shard 子集
  - W3 low-overlap：随机连续 block 窗口，窗口间少量交叠
  - W4 zero-overlap：DB 划分为互不相交的组，每 query 独占一组

- [ ] **Step 1:** 写失败测试：同 seed 输出字节一致；config 行 `format_version==1`；stream 内 block 时刻单调递增且等于 `first_block_ns + i*interval`；W4 任两 query 的 block 集合交集为空。
- [ ] **Step 2:** 运行确认失败：`python3 -m unittest discover -s tools -p "test_*.py" -t .`
- [ ] **Step 3:** 最小实现生成器（stdlib `random.Random(seed)`，零依赖）。
- [ ] **Step 4:** 测试通过；4 个 workload 生成到 `workloads/` 并人工抽查一行。
- [ ] **Step 5:** commit：`feat(trace): add msa workload trace generator`

**Verify:** 上述 unittest 全绿；生成 `workloads/w1..w4.jsonl` 成功且 W4 交集断言成立。

### Task P0.2: Trace analyzer

**Files:** Create `tools/trace_analyzer/{__init__.py,analyze.py,metrics.py}`, `tools/trace_analyzer/tests/{__init__.py,test_metrics.py}`

**Interfaces:** CLI `python3 -m tools.trace_analyzer.analyze --trace workloads/w1.jsonl --out reports/workload/w1.json --dram-fractions 0.01,0.05,0.25 --coalesce-window-ns 43000000`

- 静态指标（spec §18.3/§19）：`logical_block_requests`、`unique_blocks`、`sharing_factor = logical/unique`、per-block consumer 分布（p50/p90/p99）、query 对 Jaccard 均值
- 时间指标（本计划新增，Phase 0 Gate 的决定性依据）：per-block 相邻消费者 gap 分布；`coalescible_fraction(window)`；reuse-distance 分布 → 各 DRAM 比例下 LRU 命中率估计；每 query 对的时间重叠率
- [ ] **Step 1:** 写失败测试：手工构造 8 行 fixture，断言 `sharing_factor`、`coalescible_fraction`、LRU 命中率精确值；W4 上 `sharing_factor == 1.0`。
- [ ] **Step 2:** 运行确认失败。
- [ ] **Step 3:** 最小实现（flat 兼容解析 + streams 解析）。
- [ ] **Step 4:** 测试通过；对 W1–W4 各跑一次 → `reports/workload/*.json`。
- [ ] **Step 5:** commit：`feat(trace): add workload analyzer with temporal metrics`

**Verify:** unittest 全绿；`reports/workload/*.json` 可 `python3 -m json.tool` 解析。

### Task P0.3: Phase 0 报告 + Gate 判定

**Files:** Create `reports/workload-characterization.md`（D4）

- [ ] **Step 1:** 汇总四 workload 结果表：`sharing_factor`、`coalescible_fraction`（1×/2×/10× 传输窗口）、各 DRAM 比例 LRU 命中率、时间重叠率。
- [ ] **Step 2:** 明确回答 spec Phase 0 验收问题：“每个 physical block 平均有多少 potential consumer”。
- [ ] **Step 3:** 记录 Gate 判定：
  - 继续条件：存在 workload 使 `coalescible_fraction` 或 DRAM 复用显著 > 0；
  - 停止条件（Rule 6）：所有 workload `sharing_factor ≈ 1.0` 且 `coalescible_fraction < 5%` → 停止并上报，不进入 Phase 1 功能实现。
- [ ] **Step 4:** 注明 P0.4（真实 trace 校准）因无 Linux+真实 DB 环境暂缓，方法与所需环境写入“future validation”一节。
- [ ] **Step 5:** commit：`docs(phase0): add workload characterization report with gate decision`

**Verify:** Gate 判定为显式 PASS/STOP 二值；若 STOP，立即暂停并等用户裁决。

---

## Phase 1 — Scheduler Simulator（C++20）

### Task P1.1: CMake + GoogleTest + core 类型

**Files:** Create `CMakeLists.txt`, `core/include/msaflow/types.hpp`, `tests/unit/test_types.cpp`

**Interfaces:** §5 全部类型：`DatabaseId{uint64_t id}`、`DatabaseVersion{major,minor}`、`DatabaseMeta`、`BlockMeta`、`QueryStatus`(enum class)、`QueryState`、`BlockState`、`CacheClass`、`BlockRuntime`、`InflightRequest`、`CacheKey{db_id,db_version,block_id}` + `operator==` / hash。

- [ ] **Step 1:** 写失败测试：构造各类型、断言字段默认值；`CacheKey` 相等/哈希。
- [ ] **Step 2:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure` 确认失败（目标不存在）。
- [ ] **Step 3:** 最小实现 CMake（C++20、FetchContent GoogleTest `v1.15.2`）+ types.hpp。
- [ ] **Step 4:** 构建通过、ctest 全绿。
- [ ] **Step 5:** commit：`feat(core): add core data types and build skeleton`

**Verify:** `ctest` 全绿；构建日志无 warning（-Wall -Wextra）。

### Task P1.2: QueryRegistry + BlockAggregator + PriorityPolicy

**Files:** Create `core/include/msaflow/{query_registry.hpp,block_aggregator.hpp,priority_policy.hpp}`, `core/src/{query_registry.cpp,block_aggregator.cpp,priority_policy.cpp}`, `tests/unit/{test_query_registry.cpp,test_block_aggregator.cpp,test_priority_policy.cpp}`

**Interfaces:**
- `QueryRegistry`: `register_query(QueryState)`, `advance_cursor(id, block_id)`, `set_status(id, QueryStatus)`, `cancel(id)`；版本不匹配返回错误。
- `BlockAggregator`: 输入 `vector<{query_id,block_id}>` → `map<block_id, vector<query_id>>`。
- `enum class PolicyKind {FIFO, LRU_sharing, SHARING_ONLY, URGENCY_ONLY, MSAFLOW_V0}`；`double score(kind, const BlockRuntime&, wait_ns, io_cost_ns)`，MSAFLOW_V0 按 spec §7.3：`α*urgency + β*log2(1+consumers) - γ*io_cost`。
- 常量 `α/β/γ` 暴露为参数（默认 1.0/1.0/1.0e-6），不写死结论（spec §8.4 要求可调参）。

- [ ] **Step 1:** 失败测试：aggregation 合并同 block 消费者（spec §7.1 的 B100→Q1,Q3 例子）；FIFO 相等分数保序；sharing 高的 block 先于低 sharing；wait 增长后 urgency-only 反超（aging 无饥饿场景）。
- [ ] **Step 2:** 确认失败。
- [ ] **Step 3:** 最小实现。
- [ ] **Step 4:** ctest 全绿。
- [ ] **Step 5:** commit：`feat(scheduler): add query registry aggregation and priority policy`

**Verify:** ctest 全绿；聚合用例输出与 spec §7.1 完全一致。

### Task P1.3: CacheManager（shared / streaming / eviction）

**Files:** Create `core/include/msaflow/cache.hpp`, `core/src/cache.cpp`, `tests/unit/test_cache.cpp`

**Interfaces:** `CacheManager(capacity_blocks)`；`admit(CacheKey, CacheClass)`, `lookup(CacheKey)`, `release(CacheKey)`, `evict_to_low_watermark()`, stats（occupancy、evictions、hits by class）。`keep_score = 2*log2(1+active) + 3*log2(1+future) + recency_bonus`（spec §8.4），系数为构造参数；HIGH=90%/LOW=75% 可配。

- [ ] **Step 1:** 失败测试：active_consumers>0 的 block 不可驱逐；streaming block 在最后消费者 release 后立即可回收；容量到 HIGH 触发驱逐至 LOW；keep_score 排序正确。
- [ ] **Step 2:** 确认失败。
- [ ] **Step 3:** 最小实现（单 map + 简单扫描驱逐；不引 heap 优化，样本量不需要）。
- [ ] **Step 4:** ctest 全绿。
- [ ] **Step 5:** commit：`feat(cache): add shared streaming pools and eviction`

### Task P1.4: StorageBackend 接口 + SimulatedBackend

**Files:** Create `core/include/msaflow/buffer.hpp`, `core/include/msaflow/io_backend.hpp`, `simulator/simulated_backend.{hpp,cpp}`, `tests/unit/test_buffer.cpp`, `tests/unit/test_sim_backend.cpp`

**Interfaces:**
```cpp
class Buffer {                 // 4K 对齐，容量固定
  explicit Buffer(size_t bytes); void* data(); size_t size() const;
};
using ReadCallback = std::function<void(uint64_t io_id, uint64_t block_id, int err)>;
class StorageBackend {
 public:
  virtual ~StorageBackend() = default;
  virtual uint64_t submit_read(uint64_t block_id, Buffer* buf, ReadCallback cb) = 0;
  virtual void poll() = 0;     // 交付到期的完成事件
};
class SimulatedBackend : public StorageBackend {  // 参数：block_bytes, bandwidth_bytes_per_ns, base_latency_ns, queue_depth
};
```
- Phase 2 的 io_uring backend 实现同一接口（本任务只定义接口）。
- [ ] **Step 1:** 失败测试：QD=1 时两次 submit 串行完成；QD≥2 时完成时刻重叠；完成顺序 FIFO；错误注入路径回调 `err != 0`。
- [ ] **Step 2:** 确认失败。
- [ ] **Step 3:** 最小实现（内部 pending 队列 + 单调时钟）。
- [ ] **Step 4:** ctest 全绿。
- [ ] **Step 5:** commit：`feat(storage): add storage backend interface and simulated backend`

### Task P1.5: Simulator CLI + DES + metrics

**Files:** Create `simulator/trace_reader.{hpp,cpp}`, `simulator/des.{hpp,cpp}`, `simulator/cli.cpp`, `core/include/msaflow/metrics.hpp`, `tests/unit/test_trace_reader.cpp`, `tests/unit/test_des.cpp`

**Interfaces:**
- CLI（flag 与 spec §38 D3 一致）：`msaflow-sim --trace FILE --policy {fifo,lru,sharing,urgency,msaflow-v0} --dram-blocks N [--seed S] [--block-bytes B] [--bandwidth-gbps X] [--io-depth D] [--prefetch on|off] [--out FILE]`
- 事件：`QueryArrive` / `RequestBlock` / `BlockReady` / `QueryDone`；队列为 `std::priority_queue`（time, insertion-order）保证确定性。
- metrics（spec §18 子集）：`physical_block_reads`、`logical_block_requests`、`coalescing_ratio = 1 - physical/logical`、`shared_cache_hits`、`streaming_hits`、`prefetch_hits`、`prefetch_waste`、`evictions`、`dram_occupancy_peak`、`starvation_count`、`scheduler_decision_ns_total`、latency p50/p95/p99、`throughput_qps`；输出 JSON。
- [ ] **Step 1:** 失败测试：3-query 小 trace 下 `msaflow-v0` 的 physical reads < `fifo`；同 seed 两次运行 metrics JSON 字节一致；trace_reader flat 兼容用例。
- [ ] **Step 2:** 确认失败。
- [ ] **Step 3:** 最小实现（单线程 DES；无线程池）。
- [ ] **Step 4:** ctest 全绿；手工跑 W1 一次输出可读 JSON。
- [ ] **Step 5:** commit：`feat(sim): add discrete-event simulator with policies and metrics`

### Task P1.6: 全量单测 + starvation/确定性 golden

**Files:** Modify `tests/unit/*`；Create `tests/golden/{tiny_w1.jsonl,tiny_w1_msaflow.json}`

- [ ] **Step 1:** 补 spec §32 unit 清单：cursor 推进、inflight attach、cache admission、eviction、priority、cancellation、database version mismatch、prefetch 规则（READY/INFLIGHT/拥塞时跳过）、QD 背压、aging 无饥饿断言（构造对抗 workload 跑 N 事件后 `starvation_count == 0`）。
- [ ] **Step 2:** golden：`tiny_w1` 在小 DRAM 下 msaflow-v0 结果与提交的 JSON 完全一致（防回归）。
- [ ] **Step 3:** ctest 全绿；commit：`test(sim): add full unit suite starvation and golden coverage`

### Task P1.7: Sweep + scheduler-baseline 报告 + Gate

**Files:** Create `tools/run_sweeps.py`, `tools/report_scheduler.py`, `reports/scheduler-baseline.md`（D5）

- [ ] **Step 1:** `run_sweeps.py`：W1–W4 × policies{5} × dram{1%,5%,25%,100%} × seed{1..5} 调 `msaflow-sim`，输出 `reports/scheduler/<workload>_<policy>_dram<f>_s<seed>.json`。
- [ ] **Step 2:** `report_scheduler.py` 聚合为 markdown 表格（physical reads、coalescing ratio、p95、starvation、overhead）。
- [ ] **Step 3:** Gate 判定（spec §20）：W1/W2 上 msaflow-v0 相对 generic LRU：physical reads 更少 ∧ coalescing ratio 更高 ∧ `starvation_count == 0` ∧ `scheduler_decision_ns_total / simulated_wall_ns < 1%`。输出 PASS/FAIL。
- [ ] **Step 4:** 若 FAIL：只调 policy 参数/逻辑，不扩功能（Rule 5）；FAIL 未解决前不进入 Phase 2。
- [ ] **Step 5:** commit：`bench: add scheduler sweeps and baseline report`

---

## Phase 2 入口（本计划不实现）

Phase 2 启动条件（三条全满足才开工）：

1. Phase 1 Gate PASS，且结果已 commit；
2. 有 Linux + NVMe 主机（kernel 支持 io_uring；记录 NVMe 型号、kernel、文件系统）；
3. 用户明确下令。

届时任务切分（只写入口，不展开）：
- 2A 便携核心：C++20 runtime 骨架、FASTA→Block DB builder（`manifest.json`/`sequences.data`/`sequences.index`/`blocks.meta`，spec §6）、BufferPool、metrics、`pread` 同步 backend（macOS 可测）。
- 2B Linux NVMe：`LocalNvmeBackend`（liburing + O_DIRECT、QD 可配）、并发 8/16/32/64/128 benchmark、page cache vs O_DIRECT vs MSAFlow 对照（spec §21）。
- 交付物：`docs/phase2-entry-criteria.md` 记录环境检查表与任务切分（本轮只写该文档）。

## 任务完成定义

每个任务必须满足：测试先失败后通过、ctest/unittest 全绿、commit message 符合 spec Rule 3 风格、无超出 Files 列表的改动。Phase 0 与 Phase 1 各有一个显式 Gate，Gate 未过不得进入下一段。
