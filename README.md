# MSAFlow

Query-aware, storage-aware data plane for high-throughput protein MSA search.

- Spec: `MSAFlow_Project_Execution_Spec_v1.0.md`
- Execution plan: `docs/superpowers/plans/2026-09-18-msaflow-phase-0-2.md`
- Status: Stage R + Phase 0 + Phase 1 complete; Phase 2 gated on Linux/NVMe host

## Layout

| Path | Purpose |
| --- | --- |
| `core/` | C++20 scheduling/cache/IO-core logic (shared by simulator and future runtime) |
| `simulator/` | Discrete-event simulator, produces `msaflow-sim` |
| `tools/` | Python offline tooling: trace generation and analysis |
| `docs/` | Reading notes, plans, entry criteria |
| `workloads/` | Generated traces (JSONL, gitignored) |
| `reports/` | Phase reports and metrics JSON |
| `third_party/` | Pinned reference checkouts (see `REFERENCES.md`) |
