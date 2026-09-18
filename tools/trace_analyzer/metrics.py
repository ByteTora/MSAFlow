"""Static and temporal metrics over Trace Format v1 workloads.

Events are tuples (time_ns, query_id, block_id). "Static" metrics count reuse;
"temporal" metrics measure how much of that reuse is actually exploitable in
time (coalescing windows, DRAM reuse distance, query phase overlap).
"""

from collections import OrderedDict, defaultdict


def expand_events(queries):
    events = []
    for query in queries:
        for stream in query.get("streams", []):
            start = stream["first_block_ns"]
            step = stream["block_interval_ns"]
            for index, block in enumerate(stream["blocks"]):
                events.append((start + index * step, query["query_id"], block))
    events.sort()
    return events


def _percentile(values, q):
    if not values:
        return 0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round(q * (len(ordered) - 1))))
    return ordered[index]


def compute_static(events):
    consumers = defaultdict(int)
    for _, _, block in events:
        consumers[block] += 1
    counts = list(consumers.values())
    logical = len(events)
    unique = len(consumers)
    return {
        "logical_block_requests": logical,
        "unique_blocks": unique,
        "sharing_factor": logical / unique if unique else 0.0,
        "consumers_per_block": {
            "min": min(counts) if counts else 0,
            "p50": _percentile(counts, 0.50),
            "p90": _percentile(counts, 0.90),
            "p99": _percentile(counts, 0.99),
            "max": max(counts) if counts else 0,
            "mean": (sum(counts) / len(counts)) if counts else 0.0,
        },
    }


def coalescible_fraction(events, window_ns):
    times = defaultdict(list)
    for time_ns, _, block in events:
        times[block].append(time_ns)
    coalescible = 0
    for block_times in times.values():
        block_times.sort()
        for index, time_ns in enumerate(block_times):
            has_left = index > 0 and time_ns - block_times[index - 1] <= window_ns
            has_right = index + 1 < len(block_times) and block_times[index + 1] - time_ns <= window_ns
            if has_left or has_right:
                coalescible += 1
    return coalescible / len(events) if events else 0.0


def dram_lru_hit_rate(events, capacity_blocks):
    capacity = max(0, capacity_blocks)
    cache = OrderedDict()
    hits = 0
    for _, _, block in events:
        if block in cache:
            hits += 1
            cache.move_to_end(block)
        else:
            cache[block] = True
            while len(cache) > capacity:
                cache.popitem(last=False)
    return hits / len(events) if events else 0.0


def query_spans(queries):
    spans = {}
    for query in queries:
        duration = 0
        for stream in query.get("streams", []):
            duration = max(duration, len(stream["blocks"]) * stream["block_interval_ns"])
        spans[query["query_id"]] = (query["arrival_ns"], query["arrival_ns"] + duration)
    return spans


def compute_temporal(events, queries, windows_ns, dram_fractions, num_blocks=None):
    spans = query_spans(queries)
    ids = sorted(spans)
    overlapping = 0
    pairs = 0
    for i, first in enumerate(ids):
        for second in ids[i + 1:]:
            a, b = spans[first], spans[second]
            pairs += 1
            if a[0] < b[1] and b[0] < a[1]:
                overlapping += 1
    if num_blocks is None:
        num_blocks = (max((block for _, _, block in events), default=-1) + 1)
    times = [time_ns for time_ns, _, _ in events]
    durations = [end - start for start, end in spans.values()]
    return {
        "coalescible_fraction": {str(window): coalescible_fraction(events, window) for window in windows_ns},
        "dram_lru_hit_rate": {
            str(fraction): dram_lru_hit_rate(events, round(fraction * num_blocks))
            for fraction in dram_fractions
        },
        "query_pairs_overlapping_fraction": (overlapping / pairs) if pairs else 0.0,
        "makespan_ns": (max(times) - min(times)) if times else 0,
        "mean_query_duration_ns": (sum(durations) / len(durations)) if durations else 0.0,
    }
