# MSAFlow
## Query-aware Storage Runtime for High-Throughput Protein MSA

**版本：** v1.0
**文档类型：** 可直接交给 Coding Agent / 研发 AI 执行的项目规格书
**日期：** 2026-09-18
**项目阶段：** 从架构定义进入工程实施

---

## 0. 执行摘要

MSAFlow 的总目标不是“做一个 Cache”，而是**降低高并发蛋白质 MSA 搜索的整体 latency、提高 MSA throughput**。

第一阶段聚焦 Storage/Data Plane：在不修改 HMMER3/JackHMMER 核心搜索算法、不使用 GPU/HBM、不使用 RDMA 的前提下，把 Sequence Database 从“一个被每个搜索进程直接打开的文件”升级为由 Runtime 管理的 Block Database，并实现：

1. Query Cursor
2. Query-aware Cooperative Scan
3. In-flight Request Coalescing
4. DRAM Shared/Streaming Cache
5. One-block Prefetch
6. Async Local NVMe I/O
7. JackHMMER Storage Adapter
8. AlphaFold 3 Integration
9. 完整 Metrics / Trace / Benchmark

核心假设：

> 在多个 MSA Query 并发搜索同一大型 Sequence Database 时，存在足够的跨-query scan locality；通过让多个 Query 协同消费同一 Database Block，可以减少 physical I/O、storage wait 和数据搬运，从而降低 MSA latency、提高 throughput。

第一阶段不预设必须获得多少倍 speedup。必须先通过 workload characterization 确认：

- 跨 Query block overlap 是否足够高；
- I/O 是否真的占据可观的 MSA 时间；
- Runtime scheduler 的开销是否低于其节省的 I/O 时间；
- Linux page cache / static sharding 已经提供的收益是否足以吞掉本方案的增量收益。

只有这些问题得到正面结果，才进入 Remote Storage / RDMA / Distributed Scheduler / GPU-HMMER 阶段。

---

# 1. 背景与问题定义

## 1.1 AlphaFold 3 当前 MSA 数据路径

AlphaFold 3 官方代码把 protein MSA 搜索配置在 `src/alphafold3/data/pipeline.py` / `msa_config.py`，通过 `jackhmmer.py` 调用 HMMER JackHMMER。当前 protein MSA 的 UniRef90、MGnify、Small BFD、UniProt 配置均采用 `n_iter=1`；运行参数支持 `jackhmmer_n_cpu` 和 `jackhmmer_max_parallel_shards`。官方性能文档明确指出 genetic search 对磁盘速度敏感，并建议更快磁盘、RAM-backed filesystem、更多 CPU 和并行 shard。官方还说明 sharded genetic database 能显著加速搜索，并要求设置正确的 Z-value 以进行跨 shard 的 E-value 计算。 

官方 AF3 当前代码入口：

- `src/alphafold3/data/pipeline.py`
- `src/alphafold3/data/msa_config.py`
- `src/alphafold3/data/tools/jackhmmer.py`
- `src/alphafold3/data/tools/shards.py`
- `run_alphafold.py`
- `docs/performance.md`

参考：

- https://github.com/google-deepmind/alphafold3/blob/main/src/alphafold3/data/pipeline.py
- https://github.com/google-deepmind/alphafold3/blob/main/src/alphafold3/data/msa_config.py
- https://github.com/google-deepmind/alphafold3/blob/main/src/alphafold3/data/tools/jackhmmer.py
- https://github.com/google-deepmind/alphafold3/blob/main/docs/performance.md
- https://github.com/google-deepmind/alphafold3/blob/main/run_alphafold.py

## 1.2 当前问题

传统模式可抽象为：

```text
Q1 -> JackHMMER -> Database File -> OS/FileSystem -> NVMe
Q2 -> JackHMMER -> Database File -> OS/FileSystem -> NVMe
Q3 -> JackHMMER -> Database File -> OS/FileSystem -> NVMe
...
```

即使多个 Query 实际需要扫描同一批数据库数据，当前架构也没有专门的 MSA Storage Runtime 去管理：

- 哪个 Query 当前扫到哪个 Block；
- 哪些 Query 即将访问同一个 Block；
- 某个 Block 是否正在被另一个 Query 读取；
- DRAM 中哪些 Block 应长期保留；
- 哪些数据只应该 streaming 一次；
- 哪些 Block 值得 prefetch；
- Local NVMe 与未来 Remote Storage 的统一访问抽象。

## 1.3 正确的项目目标

不要将项目描述成：

> “优化 AF3 的 I/O”。

正式目标应写为：

> **MSAFlow 通过 Query-aware Scheduling、Cooperative Scan、Block Cache 和异步数据预取降低 Sequence Database 的数据访问开销，使 MSA Search 更少等待数据、更充分利用计算资源，从而降低 MSA latency 并提高高吞吐 workload 的 queries/sec。**

I/O 是中间优化目标，最终 KPI 是：

- MSA latency
- MSA throughput
- queries/sec
- storage wait / compute wait overlap

---

# 2. 为什么值得做

现有公开系统已经分别证明了以下方向是可行的，但尚未形成我们目标的统一 Runtime：

### 2.1 MSA 已经服务化

ColabFold 使用独立 MSA server / MMseqs2 service 来生成 MSA，证明“MSA 作为共享基础服务”是成熟的架构方向。

参考：
https://github.com/sokrypton/ColabFold
https://github.com/sokrypton/ColabFold/tree/main/MsaServer

### 2.2 Persistent Database / GPU Database Server 已经存在

MMseqs2-GPU 支持 GPU Server 和数据库缓存；NVIDIA MSA Search NIM 进一步把这一模式产品化，并在启动时缓存约 1.4 TB 数据库到本地 cache。 

参考：
https://github.com/soedinglab/MMseqs2
https://docs.nvidia.com/nim/bionemo/msa-search/latest/getting-started.html
https://docs.nvidia.com/nim/bionemo/msa-search/latest/configure.html

### 2.3 Cooperative Scan / Predictive Buffer Management 已经有数据库理论基础

数据库领域的 Cooperative Scans 与 Predictive Buffer Management 针对多个并发长扫描 Query 共享大表数据的问题，使用 Active Buffer Manager / future workload information 改善 buffer management。MSA 的“多个 Query 扫同一大型 sequence DB”与其具有非常强的结构相似性。

参考：
https://arxiv.org/abs/1208.4170

### 2.4 Block Cache / Tiered Storage 在 AI Serving 中已经成熟

vLLM 使用 block-based KV cache 与 scheduler；LMCache 将 cache 从单一内存层扩展到 CPU memory、local storage、remote backends，并通过可插拔 storage interface 实现扩展。

参考：
https://github.com/vllm-project/vllm/blob/main/docs/design/prefix_caching.md
https://github.com/vllm-project/vllm/blob/main/vllm/v1/core/block_pool.py
https://docs.lmcache.ai/
https://github.com/LMCache/LMCache

### 2.5 未来 Remote Storage / RDMA 有成熟底座

SPDK 已提供 NVMe-oF，包括 RDMA transport；Linux `io_uring` 可用于第一阶段异步 Local NVMe I/O；未来 GPU 化后可进一步使用 GPUDirect Storage。

参考：
https://github.com/spdk/spdk/blob/master/doc/nvmf.md
https://github.com/axboe/liburing
https://docs.nvidia.com/gpudirect-storage/

---

# 3. 项目范围

## 3.1 V0 必须实现

- Local NVMe
- CPU DRAM
- Sequence Database Block 化
- Query Registry
- Query Cursor
- Cooperative Scan
- Inflight Request Coalescing
- Shared Pool
- Streaming Pool
- One-block Prefetch
- Async I/O
- HMMER Adapter
- AF3 Integration
- Workload Trace
- Simulator
- Metrics
- Correctness Tests

## 3.2 V0 明确禁止提前实现

- GPU-HMMER
- MMseqs2-GPU execution path
- HBM cache
- GDS data path
- RDMA
- NVMe-oF
- Multi-node Global Scheduler
- ML scheduler
- Rewrite HMMER scoring/filter kernels
- 修改 AF3 模型
- 修改 AF3 feature semantics

原因：V0 的唯一研究问题是 Storage-aware MSA Runtime 是否本身产生可测收益。

---

# 4. 系统总架构

```text
                         AlphaFold 3
                              |
                              v
                       MSAFlow Client API
                              |
                +-------------+-------------+
                |                           |
          Query Manager                 Scheduler
                |                           |
                +-------------+-------------+
                              |
                         Block Manager
                              |
                         Cache Manager
                              |
                         Storage API
                              |
                    +---------+---------+
                    |                   |
                   DRAM            Local NVMe
                    |                   |
                    +---------+---------+
                              |
                         JackHMMER
```

设计原则：

1. Search Plane 与 Data Plane 解耦。
2. Logical DB 与 Physical Storage 解耦。
3. Query 自身扫描顺序保持不变。
4. 只改变不同 Query 之间的物理 I/O 调度。
5. Block 内容 immutable。
6. Control Plane 与 Data Plane 分离。

---

# 5. 核心数据模型

## 5.1 Logical Database

```cpp
struct DatabaseId {
    uint64_t id;
};

struct DatabaseVersion {
    uint64_t major;
    uint64_t minor;
};

struct DatabaseMeta {
    DatabaseId id;
    DatabaseVersion version;
    uint64_t sequence_count;
    uint64_t block_count;
    uint64_t block_size;
    uint64_t total_bytes;
    std::string checksum;
};
```

Cache key 至少使用：

```text
(db_id, db_version, block_id)
```

防止不同数据库版本共享错误数据。

## 5.2 BlockMeta

```cpp
struct BlockMeta {
    uint64_t block_id;
    uint64_t file_offset;
    uint32_t byte_size;
    uint64_t first_seq_id;
    uint64_t last_seq_id;
};
```

## 5.3 QueryState

```cpp
enum class QueryStatus {
    CREATED,
    RUNNABLE,
    WAITING_BLOCK,
    PROCESSING,
    DONE,
    CANCELLED,
    ERROR,
};

struct QueryState {
    uint64_t query_id;
    DatabaseId db_id;
    DatabaseVersion db_version;

    QueryStatus status;

    uint64_t current_block;
    uint64_t next_block;

    uint64_t blocks_processed;

    uint64_t created_at_ns;
    uint64_t last_progress_ns;
    uint64_t deadline_ns;
};
```

V0 只保存 current + next，不实现复杂未来预测。

## 5.4 BlockRuntime

```cpp
enum class BlockState {
    ABSENT,
    INFLIGHT,
    READY,
    IN_USE,
};

enum class CacheClass {
    NONE,
    SHARED,
    STREAMING,
};

struct BlockRuntime {
    uint64_t block_id;
    BlockState state;
    CacheClass cache_class;

    Buffer* buffer;

    uint32_t active_consumers;
    uint32_t future_consumers;

    uint64_t last_access_ns;
    uint64_t first_request_ns;

    uint64_t io_request_id;
};
```

## 5.5 InflightRequest

```cpp
struct InflightRequest {
    uint64_t io_request_id;
    uint64_t block_id;
    Buffer* buffer;

    std::vector<uint64_t> consumers;

    uint64_t submit_ns;
    uint64_t complete_ns;

    int error_code;
};
```

核心 invariant：

> 对于同一个 `(db_id, db_version, block_id)`，最多允许一个 active physical read。

---

# 6. Block Database Format

V0 建议不要让 Runtime 直接依赖大型 FASTA 文本文件。

建议使用自己的 immutable Block DB 格式：

```text
uniref90-2026/
├── manifest.json
├── sequences.data
├── sequences.index
└── blocks.meta
```

## manifest.json

记录：

- database id
- database version
- sequence count
- block count
- block size
- total bytes
- checksum
- format version

## sequences.data

连续 Sequence payload。

## sequences.index

至少记录：

```text
sequence_id
offset
length
block_id
```

## blocks.meta

记录：

```text
block_id
file_offset
byte_size
first_seq_id
last_seq_id
```

### 关键约束

- 一个 sequence 不跨 Block；
- Block immutable；
- DB version immutable；
- Block checksum 可验证；
- Sequence ID 映射必须可追溯。

---

# 7. Scheduler 设计

## 7.1 Query Aggregation

输入：

```text
Q1 -> B100
Q2 -> B300
Q3 -> B100
Q4 -> B500
Q5 -> B300
```

聚合为：

```text
B100 -> Q1,Q3
B300 -> Q2,Q5
B500 -> Q4
```

这是 Cooperative Scan 的核心。

## 7.2 Block State 处理

对每个 aggregated block：

### READY

直接让消费者使用，不产生 I/O。

### INFLIGHT

把 Query attach 到现有 request，不产生第二次 physical read。

### ABSENT

进入 scheduler I/O queue。

## 7.3 Priority

V0 使用：

```text
priority = α * urgency
         + β * sharing
         - γ * io_cost
```

其中：

```text
sharing = log2(1 + consumer_count)
```

V0 不需要 ML，也不需要复杂预测。

## 7.4 Aging / Anti-starvation

即使 sharing 很高，也必须防止单 Query starvation。

实现：

```text
urgency ↑ as waiting time ↑
```

并保证：

> 任意 runnable Query 都不能无限期等待。

---

# 8. Cache 设计

## 8.1 Shared Pool

用途：

- active consumers > 1；或
- future consumers > 0。

优先保留。

## 8.2 Streaming Pool

用途：

- 只有一个 consumer；
- 明确无未来复用。

使用完立即释放。

## 8.3 为什么不用纯 LRU

大型 sequential scan 容易造成 scan pollution。普通 LRU 只看到“刚才被访问过”，而 MSA Runtime 已经知道 Query 当前 cursor 和 next block，因此可以优先利用 future reuse。

## 8.4 Eviction

Memory high watermark，例如：

```text
HIGH = 90%
LOW  = 75%
```

达到 HIGH 时开始 eviction，直到 LOW。

Shared Pool 的优先保留指标：

```text
keep_score =
    2 * log2(1 + active_consumers)
  + 3 * log2(1 + future_consumers)
  + recency_bonus
```

Streaming Pool 一般不长期保留。

这些系数只是 V0 default，必须通过 benchmark 调参，不可写死成项目结论。

---

# 9. Prefetch

V0 只做 one-block lookahead：

```text
Q1 current = B100
Q1 next    = B101
```

如果：

- DRAM 有足够空间；
- I/O queue 不拥塞；
- B101 未 READY；
- B101 未 INFLIGHT；

则提交 prefetch(B101)。

如果多个 Query 的 next block 相同：

```text
Q1 -> B101
Q3 -> B101
Q8 -> B101
```

预取只产生一次 physical read，并将三个 Query 作为潜在消费者挂在同一个 inflight entry 上。

V0 禁止超过 one-block 的 aggressive lookahead，以避免 prefetch pollution。

---

# 10. Data Plane / Control Plane

## Control Plane

可以使用：

- Unix Domain Socket
- gRPC（后续服务化）

负责：

- register query
- cancel
- release
- metrics
- configuration

## Data Plane

V0 推荐：

- shared memory
- local DRAM buffer

不允许通过 JSON/HTTP 传递大块 Sequence 数据。

---

# 11. Local I/O

V0 采用：

- Linux `io_uring`
- `O_DIRECT`
- aligned DRAM buffer

推荐使用 `liburing` 作为 io_uring userspace helper。

参考：

https://github.com/axboe/liburing

核心 I/O pipeline：

```text
Scheduler
   |
   v
IO Queue
   |
   v
io_uring SQE
   |
   v
Local NVMe
   |
   v
CQE
   |
   v
Inflight -> READY
```

V0 需要支持多个 outstanding reads。

---

# 12. JackHMMER Adapter

目标：**只替换 sequence database access，不修改 HMMER3 search semantics。**

重点阅读 HMMER：

- `src/jackhmmer.c`
- `src/p7_pipeline.c`
- `src/hmmer.h`
- `src/impl_*`
- Easel sequence I/O (`esl_sq*`)

`jackhmmer.c` 当前包含 `esl_sq.h` / `esl_sqio.h` 等 sequence/database I/O，并维护 `P7_PIPELINE`；HMMER 的 `p7_pipeline.c` 是核心搜索 pipeline，当前代码明确包含 MSV、bias、Viterbi、Forward 等阶段。 

参考：

https://github.com/EddyRivasLab/hmmer/blob/master/src/jackhmmer.c
https://github.com/EddyRivasLab/hmmer/blob/master/src/p7_pipeline.c
https://github.com/EddyRivasLab/hmmer/blob/master/src/hmmer.h

### Adapter 目标

原始：

```text
JackHMMER
  -> sequence file reader
  -> FASTA
```

目标：

```text
JackHMMER
  -> SequenceReader abstraction
  -> MSAFlow
  -> Block
```

禁止 V0 修改：

- MSV
- Viterbi
- Forward
- E-value
- traceback
- hit inclusion semantics

---

# 13. AlphaFold 3 接入点

优先阅读：

```text
src/alphafold3/data/pipeline.py
src/alphafold3/data/msa_config.py
src/alphafold3/data/tools/jackhmmer.py
src/alphafold3/data/tools/shards.py
run_alphafold.py
```

当前官方代码已经有：

- `jackhmmer_n_cpu`
- `jackhmmer_max_parallel_shards`
- 4 个 protein DB 并行搜索
- `n_iter=1`
- shard + Z-value 机制

因此不要重写整个 AF3 data pipeline。

建议增加一个 Adapter 配置：

```text
--msa_runtime_endpoint
--msa_runtime_db_uri
```

或者在内部将：

```text
DatabaseConfig.path
```

抽象成：

```text
msa://uniref90/2026
```

第一阶段仍然只针对 Local NVMe。

---

# 14. 参考代码库

## 14.1 必须深读

### A. AlphaFold 3

用途：真实集成点、MSA 参数、shard 逻辑。

Repo：
https://github.com/google-deepmind/alphafold3

重点：

```text
src/alphafold3/data/pipeline.py
src/alphafold3/data/msa_config.py
src/alphafold3/data/tools/jackhmmer.py
src/alphafold3/data/tools/shards.py
run_alphafold.py
docs/performance.md
```

### B. HMMER3

用途：保留 search semantics，只替换 sequence data access。

Repo：
https://github.com/EddyRivasLab/hmmer

重点：

```text
src/jackhmmer.c
src/p7_pipeline.c
src/hmmer.h
src/impl_sse/
src/impl_vmx/
src/impl_neon/
easel/
```

### C. liburing

用途：V0 local async I/O。

Repo：
https://github.com/axboe/liburing

重点：

```text
src/
examples/
```

---

## 14.2 作为设计参考，不直接复制

### D. MMseqs2

用途：成熟 sequence database format、createdb/createindex、GPU Server、database persistence。

Repo：
https://github.com/soedinglab/MMseqs2

重点：

```text
src/MMseqsBase.cpp
src/commons/Parameters.cpp
src/
```

MMseqs2 官方支持 `createdb`、`createindex`、GPU search、GPU server 等能力。 

### E. AlphaFast

用途：研究 AF3 MSA backend 替换、超高吞吐 MSA pipeline。

Repo：
https://github.com/RomeroLab/alphafast

重点：

```text
run_data_pipeline.py
run_alphafold.py
```

不要把 AlphaFast 当成 MSAFlow 的实现模板；它的目标主要是用 MMseqs2-GPU 加速 MSA，而 MSAFlow 当前阶段的目标是 Storage Runtime。

### F. ColabFold

用途：MSA Server / shared MSA service 的工程参考。

Repo：
https://github.com/sokrypton/ColabFold

重点：

```text
MsaServer/
colabfold/mmseqs.py
```

### G. vLLM

用途：block cache、scheduler、cache manager 设计参考。

Repo：
https://github.com/vllm-project/vllm

重点：

```text
docs/design/prefix_caching.md
vllm/v1/core/block_pool.py
vllm/v1/core/sched/
vllm/v1/engine/core.py
```

vLLM 的 prefix cache / block pool / ref count / eviction 思想可以直接借鉴“对象模型”，但不要把 KV-cache semantics 原封不动搬到 MSA。

### H. LMCache

用途：tiered storage、storage backend plugin、独立 daemon、observability。

Repo：
https://github.com/LMCache/LMCache

重点：

```text
docs/source/developer_guide/architecture.rst
docs/source/developer_guide/extending_lmcache/storage_plugins.rst
```

### I. SPDK

用途：未来 Local/Remote NVMe、NVMe-oF、RDMA 数据面。

Repo：
https://github.com/spdk/spdk

重点：

```text
doc/nvmf.md
lib/nvme/
lib/nvmf/
examples/
```

### J. NVIDIA GPUDirect Storage

用途：未来 GPU/HBM 阶段。

Docs：
https://docs.nvidia.com/gpudirect-storage/

V0 禁止实现。

---

# 15. 文献 / 理论参考

## 15.1 Cooperative Scans / Predictive Buffer Management

核心参考：

**From Cooperative Scans to Predictive Buffer Management**

https://arxiv.org/abs/1208.4170

需要理解：

- concurrent sequential scans
- Active Buffer Manager
- cooperative scans
- predictive buffer management
- future workload information
- LRU vs PBM vs CScans

项目中对应关系：

```text
Database Scan        <-> MSA Query
Tuple/Page           <-> Sequence Block
Buffer Manager       <-> MSAFlow Cache Manager
Scan Progress        <-> Query Cursor
Future Workload      <-> next-block / pending Query
```

---

# 16. Benchmark 工作负载

V0 必须支持 synthetic replay，先不要每次都真实运行 AF3。

## 16.1 Trace Format

JSONL：

```json
{"query_id":1,"db":"uniref90","blocks":[0,1,2,3,4]}
{"query_id":2,"db":"uniref90","blocks":[0,1,2,3,4]}
{"query_id":3,"db":"uniref90","blocks":[0,5,6,7]}
```

第一版 simulator 直接消费 block trace。

## 16.2 四类 Workload

### W1 High-overlap

所有 Query 扫相同 DB 顺序。

### W2 Medium-overlap

前几个 Block 相同，后面分叉。

### W3 Low-overlap

随机 query / 不同 region。

### W4 Zero-overlap

人为构造没有 block overlap 的 workload。

目标：确定 MSAFlow 的收益边界。

---

# 17. Baseline

至少需要 4 个 baseline：

### B0 Native

原始 JackHMMER / AF3。

### B1 Native + Page Cache

允许 Linux page cache 正常工作。

### B2 Static Sharding

使用 AF3 官方 sharded database / parallel shards。

### B3 Generic LRU Block Cache

相同 Block 格式、相同 DRAM 容量，但使用普通 LRU。

### B4 MSAFlow

Cooperative Scan + inflight coalescing + shared/streaming cache + one-block prefetch。

这是最重要的公平比较。

---

# 18. Metrics

## 18.1 End-to-end

```text
msa_latency_p50
msa_latency_p95
msa_latency_p99
queries_per_second
```

## 18.2 Storage

```text
physical_bytes_read
logical_bytes_consumed
iops
read_bandwidth
read_latency
storage_wait_time
```

核心指标：

```text
I/O amplification = physical_bytes_read / logical_bytes_consumed
```

## 18.3 Shared Access

```text
logical_block_requests
physical_block_reads
coalescing_ratio
```

```text
coalescing_ratio =
1 - physical_block_reads / logical_block_requests
```

## 18.4 Cache

```text
shared_cache_hit
streaming_hit
prefetch_hit
prefetch_waste
evictions
dram_occupancy
```

## 18.5 Scheduler

```text
scheduler_decision_latency
scheduler_cpu_usage
queue_depth
starvation_count
```

---

# 19. 第一阶段执行计划

## Phase 0 — Workload Characterization

### 目标
先确认“跨-query block locality”是否存在。

### 开发

1. 构造 trace generator。
2. 生成 W1/W2/W3/W4。
3. 统计：
   - unique blocks
   - logical block requests
   - sharing factor
   - average consumer count
   - query progress overlap

### 输出

```text
reports/workload-characterization.md
reports/workload/*.json
```

### 验收

必须回答：

> 在目标 workload 下，每个 physical Block 平均有多少潜在 consumer？

如果 sharing 极低，需要重新审视 shared scan 的价值，而不是盲目继续。

---

# 20. Phase 1 — Scheduler Simulator

### 目标
不用真实 I/O 证明 scheduler policy。

实现：

```text
Query Registry
Block Aggregation
Inflight Model
Cache Model
Eviction Model
Prefetch Model
Priority Policy
```

比较：

```text
FIFO
LRU
Sharing-only
Urgency-only
MSAFlow V0
```

### 输出

```text
simulator/
reports/scheduler/
```

### 验收

至少在高/中 overlap workload 上：

- physical block reads 更少；
- coalescing ratio > generic baseline；
- 不发生 Query starvation；
- scheduler CPU overhead 足够低。

---

# 21. Phase 2 — Local Storage Runtime

### 目标
接真实 Local NVMe。

实现：

```text
Block DB
Buffer Pool
io_uring backend
Inflight table
Async completion
Shared Pool
Streaming Pool
Prefetch
Metrics
```

### 对照

```text
Page Cache
vs
O_DIRECT/io_uring
vs
MSAFlow
```

### 验收

- 真实 NVMe benchmark；
- 8/16/32/64/128 concurrent queries；
- storage wait measurable；
- physical I/O reduction measurable；
- Runtime 本身不成为 CPU bottleneck。

---

# 22. Phase 3 — HMMER Adapter

### 目标
把真实 JackHMMER 接进来。

原则：

> 只替换 database reader，不碰 HMMER3 search semantics。

### 需要修改

优先从 HMMER 的 sequence reading path 切入，不允许大规模改动：

- `src/jackhmmer.c`
- Easel sequence I/O abstraction

谨慎对待：

- `src/p7_pipeline.c`
- optimized SIMD kernels

Phase 3 不修改后两者。

### 验收

同一 query、同一 DB、同一参数：

```text
Native HMMER
vs
MSAFlow-backed HMMER
```

比较：

- hit IDs
- E-values
- alignments
- output order（在适用位置）
- final MSA

必须证明物理 I/O 调度没有改变搜索语义。

---

# 23. Phase 4 — AlphaFold 3 Integration

### 目标
让 AF3 通过 MSAFlow 访问数据库。

### 主要入口

```text
src/alphafold3/data/pipeline.py
src/alphafold3/data/msa_config.py
src/alphafold3/data/tools/jackhmmer.py
src/alphafold3/data/tools/shards.py
run_alphafold.py
```

### 第一版策略

不要重写 DataPipeline。

增加一个 thin adapter：

```text
AF3
 -> existing MSA orchestration
 -> MSAFlow-backed database reader
 -> existing JackHMMER
```

### 验收

比较：

```text
Native AF3
vs
AF3 + MSAFlow
```

至少测试：

```text
1
4
16
32
64
128 queries
```

并记录：

- MSA latency
- end-to-end latency
- I/O bytes
- CPU utilization
- DRAM usage
- throughput
- correctness

---

# 24. Phase 5 — Scheduler V1

只有 Phase 4 证明 V0 有收益后才开始。

加入：

- 多 block lookahead
- adaptive prefetch
- future-use prediction
- workload-aware eviction
- query aging
- storage cost model

理论依据：Predictive Buffer Management。

不要在 V0 就实现。

---

# 25. Phase 6 — Remote Storage

加入：

```text
Remote NVMe
NVMe-oF
RDMA
```

底层优先研究 SPDK，而不是自己实现 RDMA storage protocol。

设计：

```text
MSAFlow Scheduler
       |
       +---- Local DRAM
       +---- Local NVMe
       +---- Remote NVMe / NVMe-oF
```

Scheduler 将 `io_cost` 从单一 local cost 扩展成 tier-aware cost。

---

# 26. Phase 7 — Distributed Runtime

加入：

- 多 Runtime Worker
- Data-locality aware routing
- worker DRAM residency
- remote DB sharing
- global queue
- local scheduler

目标：

```text
100 AF3 workers
      |
      v
Shared Sequence DB Runtime
```

而不是每台 worker 独立保存完整 database。

---

# 27. Phase 8 — GPU / HBM

最后再接：

- MMseqs2-GPU
- GPU-HMMER
- HBM block cache
- GDS
- Remote Storage -> GPU data path

Search Backend 与 MSAFlow API 不应需要重新设计。

目标结构：

```text
MSAFlow
   |
   +-- JackHMMER CPU
   +-- MMseqs2 CPU
   +-- MMseqs2 GPU
   +-- GPU-HMMER
```

Storage hierarchy：

```text
HBM
 ↓
DRAM
 ↓
Local NVMe
 ↓
Remote NVMe/RDMA
```

---

# 28. 建议目录结构

```text
msaflow/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── execution-plan.md
│   ├── benchmark.md
│   ├── correctness.md
│   └── design-notes/
│
├── core/
│   ├── query/
│   ├── block/
│   ├── scheduler/
│   ├── cache/
│   └── metrics/
│
├── storage/
│   ├── include/
│   └── local_nvme/
│
├── database/
│   ├── builder/
│   ├── manifest/
│   └── index/
│
├── adapters/
│   └── hmmer/
│
├── simulator/
├── benchmark/
├── integration/
│   └── alphafold3/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── correctness/
│   └── benchmark/
└── scripts/
```

---

# 29. Build / Dependency 建议

V0 建议：

```text
C++20
CMake >= 3.20
GCC/Clang recent version
Linux x86_64
NVMe SSD
liburing
GoogleTest
Prometheus client / OpenTelemetry（按实际选择）
Python 3.12+ for tooling
```

第一阶段避免引入大型分布式依赖。

不要因为以后需要 RDMA 就在 V0 引入 SPDK 全栈。

---

# 30. Repo Initialization

执行 AI 第一件事情：建立 `third_party/REFERENCES.md`，记录所有外部 Repo 和当前 commit。

建议：

```bash
git clone https://github.com/google-deepmind/alphafold3.git third_party/alphafold3
git clone https://github.com/EddyRivasLab/hmmer.git third_party/hmmer
git clone https://github.com/soedinglab/MMseqs2.git third_party/mmseqs2
git clone https://github.com/axboe/liburing.git third_party/liburing
git clone https://github.com/spdk/spdk.git third_party/spdk

git clone https://github.com/vllm-project/vllm.git third_party/vllm
git clone https://github.com/LMCache/LMCache.git third_party/LMCache
git clone https://github.com/sokrypton/ColabFold.git third_party/ColabFold
git clone https://github.com/RomeroLab/alphafast.git third_party/alphafast
```

然后对每个 Repo：

```bash
git -C third_party/<repo> rev-parse HEAD
```

把 commit hash 写入：

```text
third_party/REFERENCES.md
```

执行 AI **不得假设 `main/master` 永远稳定**。

每次基准实验必须记录：

- MSAFlow commit
- AF3 commit
- HMMER commit
- compiler version
- kernel version
- NVMe model
- CPU model
- RAM size
- filesystem
- benchmark configuration

---

# 31. 推荐阅读顺序

执行 AI 不要一次把所有 Repo 全读完。

## 第一优先级

1. AF3 `docs/performance.md`
2. AF3 `pipeline.py`
3. AF3 `jackhmmer.py`
4. HMMER `jackhmmer.c`
5. HMMER `p7_pipeline.c`
6. liburing basic examples

## 第二优先级

7. vLLM `prefix_caching.md`
8. vLLM `block_pool.py`
9. LMCache architecture
10. Cooperative Scan / PBM paper

## 第三优先级

11. MMseqs2 DB format / createindex
12. AlphaFast
13. ColabFold MsaServer
14. SPDK NVMe-oF
15. NVIDIA GDS

理由：

> 先理解当前系统，再理解缓存思想，再看远程存储和 GPU。

---

# 32. 测试分层

## Unit Tests

必须测试：

- Query cursor
- Block aggregation
- inflight attach
- cache admission
- eviction
- priority
- cancellation
- database version mismatch

## Integration Tests

必须测试：

- local NVMe read
- concurrent block reads
- inflight coalescing
- prefetch
- backpressure

## Correctness Tests

必须测试：

```text
Native HMMER
vs
MSAFlow HMMER
```

至少覆盖：

- no hit
- one hit
- many hits
- duplicate sequences
- long sequence
- deep MSA
- multiple queries
- database shard

## Stress Tests

建议：

```text
128 / 256 / 512 concurrent queries
```

观察：

- memory leak
- deadlock
- queue starvation
- inflight table growth
- scheduler overhead

---

# 33. Correctness 不可破坏原则

MSAFlow 不得通过减少实际搜索的 target sequences 来换取速度。

V0 不允许：

- approximate search
- sequence filtering
- early top-K pruning
- changing HMMER thresholds
- changing E-value semantics
- changing database contents
- silently dropping blocks

第一阶段如果物理 Block 顺序被 scheduler 改变，只要每个 Query 的逻辑 target coverage 完整且搜索过程不变，则属于允许优化。

---

# 34. 关键风险

## R1：跨 Query overlap 太低

结果：shared scan 几乎无收益。

应对：

- 先做 workload trace；
- 不要靠人工构造的高 overlap workload 证明价值；
- 使用真实高吞吐 Query trace。

## R2：Linux page cache 已经足够好

结果：Runtime 与 page cache 差距很小。

应对：

- 必须加入 B1 baseline；
- 研究 MSAFlow 能否通过 single-flight、query-aware admission 和 cooperative scheduling 在 page cache 之上进一步降低 physical I/O。

## R3：JackHMMER I/O 并不是主瓶颈

结果：MSA compute 占主要时间。

应对：

- 保留 Storage Runtime；
- 先量化 storage wait；
- 后续接 GPU-HMMER/MMseqs2-GPU；
- 不把“Storage Runtime 必须独立贡献巨大 speedup”写成预设结论。

## R4：Runtime overhead 太大

应对：

- C++ hot path；
- Python 只做 control/tooling；
- 不在每个 sequence 上做 RPC；
- block-level operations；
- lock contention benchmark。

## R5：Remote Storage 复杂度过高

应对：

先完成 local NVMe，再接 SPDK/NVMe-oF。

---

# 35. 关键 KPI

项目最终看这五项：

### KPI-1 MSA Latency

最终目标：下降。

### KPI-2 MSA Throughput

目标：queries/sec 提高。

### KPI-3 I/O Amplification

目标：下降。

### KPI-4 Coalescing Ratio

目标：证明多个 Query 共享 physical read。

### KPI-5 Scheduler Overhead

目标：远小于 saved I/O / compute stall。

不要只报告：

```text
cache hit rate
```

必须报告：

```text
latency + throughput + physical bytes/query
```

---

# 36. 第一版完成标准

V0 只有满足以下全部条件才算完成：

1. 有可重复的 MSA workload trace。
2. 有 Scheduler simulator。
3. 有真实 Block DB。
4. 有真实 Local NVMe async backend。
5. 有 inflight coalescing。
6. 有 Shared / Streaming Pool。
7. 有 one-block prefetch。
8. 有完整 metrics。
9. 有 Native/PageCache/LRU 三类 baseline。
10. 有 HMMER adapter。
11. 有 AF3 integration。
12. Strict correctness test 通过。
13. Multi-query benchmark 得到稳定可复现实验结果。
14. 生成完整 benchmark report。

---

# 37. 执行 AI 的强制工作规则

这个部分直接作为 Coding Agent 的执行约束。

## Rule 1：先读现有代码，禁止凭印象重写

必须先阅读并总结：

```text
AF3 pipeline.py
AF3 jackhmmer.py
AF3 msa_config.py
HMMER jackhmmer.c
HMMER p7_pipeline.c
```

然后再动代码。

## Rule 2：先 Benchmark，后优化

禁止在 Phase 0/1 尚未得到 trace 和 baseline 前实现复杂 Scheduler。

## Rule 3：每完成一个 Phase 必须提交

commit 形式建议：

```text
feat(trace): add msa workload trace generator
feat(sim): add cooperative scan simulator
feat(storage): add local nvme backend
feat(cache): add shared streaming pools
feat(scheduler): add inflight request coalescing
feat(hmmer): add msaflow sequence reader
feat(af3): integrate msaflow runtime
bench: add high concurrency benchmark
```

## Rule 4：记录实验环境

每次 benchmark 生成：

```text
benchmark_runs/<timestamp>/metadata.json
benchmark_runs/<timestamp>/results.json
benchmark_runs/<timestamp>/report.md
```

## Rule 5：禁止过早扩展

Phase 2 前不允许实现 RDMA。

Phase 4 前不允许实现 Distributed Scheduler。

Phase 4 correctness 通过前不允许实现 GPU-HMMER。

## Rule 6：发现核心假设不成立时停下来

如果 workload trace 显示：

```text
block overlap ≈ 0
```

则不得继续堆 cache/scheduler 功能。

必须先重新评估项目价值。

---

# 38. 建议第一周交付物

## D1

`docs/repo-reading-notes.md`

记录：

- AF3 MSA execution path
- HMMER database read path
- 当前 shard 行为
- candidate integration points

## D2

`tools/trace_generator/`

生成 W1-W4 workload。

## D3

`simulator/`

可运行：

```bash
./msaflow-sim \
  --trace workloads/w1.jsonl \
  --policy msaflow-v0 \
  --dram-blocks 128
```

## D4

`reports/workload-characterization.md`

## D5

`reports/scheduler-baseline.md`

这五项完成后再做真实 I/O。

---

# 39. 建议第二阶段交付物

```text
core/
storage/local_nvme/
cache/
scheduler/
benchmark/storage/
```

可以运行：

```bash
./msaflow-runtime \
  --db ./data/uniref90-msaflow \
  --dram 64G \
  --block-size 128M \
  --io-depth 32 \
  --prefetch 1
```

必须输出：

```text
physical reads
logical reads
coalesced requests
cache hit
prefetch hit
P50/P95/P99
throughput
```

---

# 40. 未来架构

最终目标不是 AF3 专用程序，而是：

```text
                         MSAFlow
                            |
                 +----------+----------+
                 |                     |
          Storage Plane          Search Plane
                 |                     |
       +---------+---------+     +----+----+
       |         |         |     |         |
      DRAM    Local NVMe Remote  HMMER   MMseqs2
                          NVMe          GPU-HMMER
                           |
                          RDMA
```

后续还可以自然支持：

- OpenFold3
- ColabFold
- other sequence search workloads
- Template search
- protein database services

项目长期定位：

> **MSAFlow is a storage-aware data plane for high-throughput protein sequence search, not an AlphaFold-specific cache.**

---

# 41. 最终交付物清单

最终仓库至少应包含：

```text
README.md
LICENSE
CHANGELOG.md
CONTRIBUTING.md

src/core/
src/storage/
src/cache/
src/scheduler/
src/database/
src/adapters/hmmer/
src/integration/alphafold3/

benchmarks/
simulator/
tests/

docs/architecture.md
docs/execution-plan.md
docs/correctness.md
docs/benchmark.md

third_party/REFERENCES.md
reports/
benchmark_runs/
```

---

# 42. 外部参考资料清单

## 核心生产代码

- AlphaFold 3: https://github.com/google-deepmind/alphafold3
- HMMER: https://github.com/EddyRivasLab/hmmer
- MMseqs2: https://github.com/soedinglab/MMseqs2
- liburing: https://github.com/axboe/liburing
- SPDK: https://github.com/spdk/spdk

## AI Serving Cache / Scheduler

- vLLM: https://github.com/vllm-project/vllm
- vLLM prefix cache design: https://github.com/vllm-project/vllm/blob/main/docs/design/prefix_caching.md
- LMCache: https://github.com/LMCache/LMCache

## MSA Service / AF3-related reference

- ColabFold: https://github.com/sokrypton/ColabFold
- AlphaFast: https://github.com/RomeroLab/alphafast
- NVIDIA MSA NIM: https://docs.nvidia.com/nim/bionemo/msa-search/latest/

## Storage / GPU

- SPDK NVMe-oF: https://github.com/spdk/spdk/blob/master/doc/nvmf.md
- GPUDirect Storage: https://docs.nvidia.com/gpudirect-storage/

## Research

- Cooperative Scans / Predictive Buffer Management: https://arxiv.org/abs/1208.4170

---

# 43. License / Compliance 注意事项

AlphaFold 3 source code 当前采用 Apache License 2.0，但模型参数受单独的 AlphaFold 3 Model Parameters Terms of Use 约束；不要将 weights、restricted assets 或未经授权的模型资源放入 MSAFlow 仓库或发布包。

HMMER 当前源码为 BSD 3-Clause，但其源码还包含 Easel、SIMD 相关及其他第三方许可说明；任何二次分发都必须保留对应 notices。

MMseqs2 当前 LICENSE.md 标明 MIT License。

执行 AI 必须：

1. 保留 third-party LICENSE；
2. 不将 AF3 weights 提交到 git；
3. 在发布前生成 dependency/license inventory；
4. 记录所有 vendored code 的来源与 commit。

---

# 44. 给执行 AI 的最终任务描述

你需要实现一个名为 **MSAFlow** 的高吞吐 MSA Storage Runtime。

第一阶段不要做 GPU、HBM、RDMA 和分布式调度。

先按下面顺序执行：

```text
Phase 0
Workload Characterization
        ↓
Phase 1
Scheduler Simulator
        ↓
Phase 2
Local NVMe Runtime
        ↓
Phase 3
JackHMMER Adapter
        ↓
Phase 4
AlphaFold 3 Integration
```

每个 Phase：

1. 先阅读相关 reference repo；
2. 输出设计记录；
3. 写最小实现；
4. 写测试；
5. 跑 benchmark；
6. 生成 report；
7. commit；
8. 才进入下一 Phase。

你必须优先证明以下核心假设：

> **Multiple MSA queries sharing the same sequence database create enough block-level locality that cooperative scheduling and block-level request coalescing can reduce physical I/O and MSA latency.**

最终成功标准不是“cache hit 很高”，而是：

```text
MSA latency        ↓
MSA throughput     ↑
physical bytes/Q   ↓
I/O amplification  ↓
coalescing ratio   ↑
```

同时：

```text
MSA correctness    = baseline
single-query cost  ≈ baseline
scheduler overhead << saved I/O / wait
```

不要为了性能修改 HMMER3 搜索逻辑，也不要在没有 workload evidence 的情况下继续增加 Scheduler 复杂度。

---

# 45. 一句话定义

**MSAFlow = 一个面向高吞吐蛋白质序列搜索的 Query-aware、Storage-aware 数据平面，把“每个 Query 自己扫 Sequence DB”变成“多个 Query 协同消费由 Runtime 管理的 Sequence Blocks”，最终目标是降低 MSA latency、提高 MSA throughput。**
