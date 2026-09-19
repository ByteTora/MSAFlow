# Benchmark runs

Per spec Rule 4, every benchmark run records its environment and results here:

```
benchmark_runs/<timestamp>/
├── metadata.json    # git commits, compiler, kernel, CPU, RAM, NVMe model, FS, build type
├── results.json     # raw measurements / per-config results
└── report.md        # human-readable summary
```

The directory is intentionally not pre-populated. Runs are created by the Phase 2
storage benchmark and by future Phase 3/4 correctness and throughput benchmarks
(spec §21, §30, §36, §41).

`metadata.json` must capture at minimum:

- MSAFlow commit (`git -C . rev-parse HEAD`)
- pinned reference commits from `third_party/REFERENCES.md` (AF3, HMMER, Easel, liburing, ...)
- compiler name + version, build type
- Linux kernel version
- NVMe model + capacity, filesystem, mount options
- CPU model + core count, RAM
- benchmark configuration (block size, DRAM budget, IO depth, concurrency levels, policy)
