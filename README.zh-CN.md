# MSAFlow

面向高吞吐蛋白质 MSA 搜索的 Query-aware、Storage-aware 数据平面。

MSAFlow 把 Sequence Database 从「每个搜索进程各自直接打开的文件」升级为「由 Runtime
管理的 Block Database」，让多个并发 MSA query 协同消费数据库 block：Query Cursor、
Cooperative Scan、In-flight Request Coalescing、Shared/Streaming DRAM 缓存、One-block
Prefetch、异步本地 NVMe I/O —— 且**不改动 HMMER3 搜索语义**。

[English](README.md) · [Spec](MSAFlow_Project_Execution_Spec_v1.0.md) · [执行计划](docs/superpowers/plans/2026-09-18-msaflow-phase-0-2.md)

## 状态

| 阶段 | 状态 |
| --- | --- |
| Stage R — 参考代码精读 | 完成 |
| Phase 0 — 工作负载刻画 | 完成（Gate：PASS，有条件） |
| Phase 1 — 调度器模拟器 | 完成（Gate：PASS） |
| Phase 2 — 本地 NVMe 运行时 | 等待 Linux + NVMe 主机 |

Phase 2 卡在环境，不是代码。`StorageBackend` 接口
（`core/include/msaflow/io_backend.hpp`）与入口条件已写在
[`docs/phase2-entry-criteria.md`](docs/phase2-entry-criteria.md)。

## 为什么值得做

现有系统已分别证明各组件可行 —— ColabFold/MMseqs2 把 MSA 服务化、vLLM/LMCache 把
block/tiered 缓存做成熟、数据库领域对 Cooperative Scan 有形式化基础 —— 但没有一个把它
们组合成统一的 MSA 存储运行时。MSAFlow 的最终 KPI 是：MSA latency ↓、throughput ↑、
physical bytes/query ↓、coalescing ratio ↑，且正确性与 baseline 一致。

## 目前的关键结论

在 trace-replay 下，**调度策略几乎不起作用**：5 个策略读到的 physical block 数相同或几乎
相同。收益来自 **single-flight coalescing**（W1 上 msaflow-v0 读 4096 block，无 coalesce
基线读 6240，1.52×）——不是来自 I/O 排序 score 公式。这印证了 spec Rule 5：V0 不该堆
调度复杂度。真实世界的价值仍需 P0.4 真实 trace 校准（等 Linux + 真实数据库环境）。
详见 [`reports/scheduler-baseline.md`](reports/scheduler-baseline.md) 与
[`reports/workload-characterization.md`](reports/workload-characterization.md)。

## 目录

| 路径 | 用途 |
| --- | --- |
| `core/` | C++20 调度/缓存/IO 核心（模拟器与未来运行时共用） |
| `simulator/` | 离散事件模拟器；产出 `msaflow-sim` |
| `tools/` | Python 离线工具：trace 生成、分析、sweep |
| `tests/` | GoogleTest 单元测试 + golden 固定样例 |
| `docs/` | 架构、精读笔记、计划、入口条件、工具链 |
| `reports/` | 各阶段报告与指标 JSON |
| `third_party/` | 钉住的参考代码检出 —— 见 `third_party/REFERENCES.md` |
| `.agents/skills/` | 项目级 coding-agent skills —— 见 `docs/toolchain.md` |

## 构建与测试

需要 CMake ≥3.20、C++20 编译器、Python 3.11+。

```bash
# 构建 + 单元测试（GoogleTest 经 FetchContent 获取；47 项）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

# 跑模拟器
./build/simulator/msaflow-sim --trace workloads/w1.jsonl \
  --policy msaflow-v0 --dram-blocks 512 --block-bytes 134217728 \
  --bandwidth-gbps 3 --io-depth 32

# 生成 trace（w1..w4）
python3 -m tools.trace_generator.generate --workload w1 --seed 7 --out workloads/w1.jsonl

# 分析 trace
python3 -m tools.trace_analyzer.analyze --trace workloads/w1.jsonl --out reports/workload/w1.json

# 调度 sweep + 报告
python3 tools/run_sweeps.py
python3 tools/report_scheduler.py
```

## 阶段路线

| Phase | 内容 | 状态 |
| --- | --- | --- |
| 0 | 工作负载刻画 | 完成 |
| 1 | 调度器模拟器 | 完成 |
| 2 | 本地 NVMe 运行时（io_uring） | 等待环境 |
| 3 | JackHMMER 适配器（仅替换 reader seam） | — |
| 4 | AlphaFold 3 集成 | — |
| 5 | Scheduler V1（多 block lookahead、自适应 prefetch） | — |
| 6 | 远程存储（NVMe-oF / RDMA） | — |
| 7 | 分布式运行时 | — |
| 8 | GPU / HBM | — |

## 文档

- [`docs/architecture-understanding.md`](docs/architecture-understanding.md) —— 九问架构报告
  （目标、HMMER 读取链路、AF3 pipeline、运行时数据流、生命周期、io_uring 路径、风险、
  缺证据、假设）。
- [`docs/repo-reading-notes.md`](docs/repo-reading-notes.md) —— 每条结论的 file:line 证据。
- [`docs/phase2-entry-criteria.md`](docs/phase2-entry-criteria.md) —— Linux/NVMe 环境清单。
- [`docs/toolchain.md`](docs/toolchain.md) —— 已安装的 coding-agent skills。

## License

[Apache-2.0](LICENSE)。`third_party/` 下的参考检出保留各自许可证；不含 AlphaFold 3
权重或受限资产。