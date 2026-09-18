"""Workload policies for Trace Format v1.

Each policy returns, per query, a list of contiguous block segments. Segments
are intersected with shard ranges to produce per-shard streams, mirroring the
way AF3 fans one query out across shards as parallel sequential scanners.
"""


def shard_ranges(num_blocks, num_shards):
    base, rem = divmod(num_blocks, num_shards)
    ranges, start = [], 0
    for shard_id in range(num_shards):
        size = base + (1 if shard_id < rem else 0)
        ranges.append((shard_id, start, start + size))
        start += size
    return ranges


def _segment_for_shards(ranges, first_shard, last_shard):
    start = ranges[first_shard][1]
    end = ranges[last_shard - 1][2]
    return (start, end)


def _all_shared(config, query_id, rng):
    return [(0, config["num_blocks"])]


def _prefix_then_split(config, query_id, rng):
    num_shards = config["num_shards"]
    params = config.get("shard_policy_params", {})
    prefix = params.get("prefix_shards", max(1, num_shards // 2))
    groups = params.get("groups", max(1, min(4, num_shards - prefix)))
    ranges = shard_ranges(config["num_blocks"], num_shards)
    rest = list(range(prefix, num_shards))
    per_group = max(1, len(rest) // groups)
    group = query_id % groups
    lo = prefix + group * per_group
    hi = prefix + (group + 1) * per_group if group < groups - 1 else num_shards
    segments = [_segment_for_shards(ranges, 0, prefix)]
    if lo < hi:
        segments.append(_segment_for_shards(ranges, lo, hi))
    return segments


def _random_windows(config, query_id, rng):
    params = config.get("shard_policy_params", {})
    length = max(1, round(params.get("window_fraction", 0.1) * config["num_blocks"]))
    start = rng.randrange(0, config["num_blocks"] - length + 1)
    return [(start, start + length)]


def _disjoint_groups(config, query_id, rng):
    num_queries = config["num_queries"]
    base, rem = divmod(config["num_blocks"], num_queries)
    start = query_id * base + min(query_id, rem)
    size = base + (1 if query_id < rem else 0)
    return [(start, start + size)]


_POLICIES = {
    "all_shared": _all_shared,
    "prefix_then_split": _prefix_then_split,
    "random_windows": _random_windows,
    "disjoint_groups": _disjoint_groups,
}


def _streams(segments, ranges, arrival_ns, interval_ns):
    per_shard = {}
    for seg_start, seg_end in segments:
        for shard_id, shard_start, shard_end in ranges:
            lo = max(seg_start, shard_start)
            hi = min(seg_end, shard_end)
            if lo < hi:
                per_shard.setdefault(shard_id, []).extend(range(lo, hi))
    return [
        {
            "shard_id": shard_id,
            "first_block_ns": arrival_ns,
            "block_interval_ns": interval_ns,
            "blocks": blocks,
        }
        for shard_id, blocks in sorted(per_shard.items())
    ]


def _interval_ns(scan_cfg, rng):
    interval = scan_cfg["block_interval_ns"]
    cv = scan_cfg.get("jitter_cv", 0.0)
    factor = rng.lognormvariate(0.0, cv) if cv > 0 else 1.0
    return max(1, round(interval * factor))


def build_query_specs(config, rng):
    policy = _POLICIES[config["shard_policy"]]
    ranges = shard_ranges(config["num_blocks"], config["num_shards"])
    arrival_mean = config["arrival"].get("mean_ns", 0)
    arrival_ns = 0
    specs = []
    for query_id in range(config["num_queries"]):
        if query_id > 0 and arrival_mean > 0:
            arrival_ns += max(1, round(rng.expovariate(1.0 / arrival_mean)))
        interval_ns = _interval_ns(config["scan"], rng)
        segments = policy(config, query_id, rng)
        specs.append(
            {
                "type": "query",
                "query_id": query_id,
                "db": config["db"],
                "arrival_ns": arrival_ns,
                "streams": _streams(segments, ranges, arrival_ns, interval_ns),
            }
        )
    return specs
