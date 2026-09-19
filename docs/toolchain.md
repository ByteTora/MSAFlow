# MSAFlow Coding Agent Toolchain

Project-scoped skills installed via the Vercel Skills CLI into `.agents/skills/`.
Reproducible pin: `skills-lock.json` (content hashes). Restore with `npx skills@latest experimental_install`.

Installed: 2026-09-19. Agent target: OpenCode.

## Skill set

| Skill | Source repo | Path | Now / later |
| --- | --- | --- | --- |
| modern-cpp | trailofbits/skills | `plugins/modern-cpp/skills/modern-cpp` | now |
| cpp-unit-testing | sentenz/skills | `skills/cpp-unit-testing` | now |
| cpp-benchmark-testing | sentenz/skills | `skills/cpp-benchmark-testing` | now |
| cmake | mohitmishra786/low-level-dev-skills | `skills/build-systems/cmake` | now |
| clang | mohitmishra786/low-level-dev-skills | `skills/compilers/clang` | now |
| sanitizers | mohitmishra786/low-level-dev-skills | `skills/runtimes/sanitizers` | now (clang) |
| io-uring | mohitmishra786/low-level-dev-skills | `skills/async-io/io-uring` | Phase 2 (read now) |
| gcc | mohitmishra786/low-level-dev-skills | `skills/compilers/gcc` | Phase 2 (Linux) |
| gdb | mohitmishra786/low-level-dev-skills | `skills/debuggers/gdb` | Phase 2 (Linux) |
| linux-perf | mohitmishra786/low-level-dev-skills | `skills/profilers/linux-perf` | Phase 2 (Linux) |

## Notes

- `gcc` / `gdb` / `linux-perf` are Linux-only. Dev workstation is macOS + Apple Clang 16; these are installed for the Phase 2 Linux/NVMe host, not for current Phase 0/1 work.
- `io-uring` is read-now, used Phase 2 (the `StorageBackend` interface in `core/include/msaflow/io_backend.hpp` is the seam).
- Skills load at agent startup. Content hashes in `skills-lock.json` pin exact versions.

Update: `npx skills@latest update`