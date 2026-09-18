# Third-party reference pins

Cloned: 2026-09-18. Re-run `git -C third_party/<repo> rev-parse HEAD` to verify.
Every benchmark run must record these commits (spec §30).

| Project | Repo | Branch | Commit | Purpose |
| --- | --- | --- | --- | --- |
| AlphaFold 3 | https://github.com/google-deepmind/alphafold3 | `main` | `c0f97eda2f1f482fd94d3a38bece18c7069b4a5c` | Real integration target: MSA pipeline, shard logic |
| HMMER 3 | https://github.com/EddyRivasLab/hmmer | `master` | `9acd8b6758a0ca5d21db6d167e0277484341929b` | Sequence-read seam for adapter; search semantics baseline |
| liburing | https://github.com/axboe/liburing | `master` | `78dce99b660fd1caaf70ef6573a88bae9bbf6476` | io_uring userspace helper for Phase 2 |
| vLLM | https://github.com/vllm-project/vllm | `main` | `67a8a3f9269795d683868254d134a199c29aff4f` | Block pool / scheduler / ref-count design reference |
| LMCache | https://github.com/LMCache/LMCache | `dev` | `92a40871d031a164262d4477700c319a14fb604d` | Tiered storage / storage plugin design reference |

Notes:

- LMCache documentation paths referenced by the spec (`docs/source/developer_guide/extending_lmcache/storage_plugins.rst`) exist on `dev`, not on `main` (404 on main). Pin branch is `dev`.
- vLLM and LMCache are partial clones (`--filter=blob:none`); file contents are fetched lazily. Commit hashes are full.
- liburing checkout reports a case-collision warning on case-insensitive filesystems (`man/IO_URING_CHECK_VERSION.3` vs `man/io_uring_check_version.3`). Harmless for reference reading; never build in-tree from this checkout.
- Reference checkouts are read-only inputs. MSAFlow code must not import from `third_party/` paths.
