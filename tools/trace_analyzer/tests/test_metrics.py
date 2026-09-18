import os
import tempfile
import unittest

from tools.trace_analyzer import analyze, metrics


def query(query_id, streams, arrival_ns=0):
    return {
        "type": "query",
        "query_id": query_id,
        "db": "test",
        "arrival_ns": arrival_ns,
        "streams": streams,
    }


def stream(blocks, first_block_ns, interval_ns, shard_id=0):
    return {
        "shard_id": shard_id,
        "first_block_ns": first_block_ns,
        "block_interval_ns": interval_ns,
        "blocks": blocks,
    }


def events(queries):
    return metrics.expand_events(queries)


class TestStaticMetrics(unittest.TestCase):
    def test_sharing_factor_counts_logical_over_unique(self):
        queries = [
            query(0, [stream([0, 1, 2], 0, 10)]),
            query(1, [stream([0, 1, 2], 0, 10)]),
        ]
        result = metrics.compute_static(events(queries))
        self.assertEqual(result["logical_block_requests"], 6)
        self.assertEqual(result["unique_blocks"], 3)
        self.assertAlmostEqual(result["sharing_factor"], 2.0)

    def test_disjoint_trace_sharing_factor_is_one(self):
        queries = [
            query(0, [stream([0, 1], 0, 10)]),
            query(1, [stream([2, 3], 0, 10)]),
        ]
        result = metrics.compute_static(events(queries))
        self.assertAlmostEqual(result["sharing_factor"], 1.0)
        self.assertEqual(result["consumers_per_block"]["p50"], 1)


class TestTemporalMetrics(unittest.TestCase):
    def test_coalescible_fraction_window_sensitivity(self):
        queries = [
            query(0, [stream([0], 0, 10)]),
            query(1, [stream([0], 10, 10)]),
        ]
        evs = events(queries)
        self.assertAlmostEqual(metrics.coalescible_fraction(evs, 20), 1.0)
        self.assertAlmostEqual(metrics.coalescible_fraction(evs, 5), 0.0)

    def test_disjoint_trace_never_coalescible(self):
        queries = [
            query(0, [stream([0], 0, 10)]),
            query(1, [stream([1], 0, 10)]),
        ]
        self.assertAlmostEqual(metrics.coalescible_fraction(events(queries), 10**12), 0.0)

    def test_dram_lru_hit_rate(self):
        queries = [
            query(0, [stream([0], 0, 10)]),
            query(1, [stream([0], 10, 10)]),
        ]
        self.assertAlmostEqual(metrics.dram_lru_hit_rate(events(queries), 1), 0.5)
        self.assertAlmostEqual(metrics.dram_lru_hit_rate(events(queries), 0), 0.0)

    def test_query_pair_overlap(self):
        queries = [
            query(0, [stream([0], 0, 100)]),
            query(1, [stream([1], 50, 100)]),
            query(2, [stream([2], 10**9, 100)], arrival_ns=10**9),
        ]
        result = metrics.compute_temporal(events(queries), queries, [20], [0.1])
        self.assertAlmostEqual(result["query_pairs_overlapping_fraction"], 1 / 3)


class TestTraceParsing(unittest.TestCase):
    def test_streams_trace_roundtrip(self):
        path = _write_trace(
            [
                {"type": "config", "format_version": 1, "db": "test", "num_blocks": 4,
                 "num_shards": 2, "block_bytes": 1000, "seed": 1},
                query(0, [stream([0, 1], 0, 10, shard_id=0), stream([2], 0, 10, shard_id=1)]),
            ]
        )
        config, queries = analyze.read_trace(path)
        self.assertEqual(config["num_blocks"], 4)
        self.assertEqual(len(queries), 1)
        evs = metrics.expand_events(queries)
        self.assertEqual(len(evs), 3)

    def test_flat_spec_compat(self):
        path = _write_trace(
            [{"query_id": 1, "db": "test", "blocks": [0, 1, 2]}]
        )
        config, queries = analyze.read_trace(path, default_interval_ns=100)
        self.assertEqual(len(queries), 1)
        evs = metrics.expand_events(queries)
        self.assertEqual([e[2] for e in evs], [0, 1, 2])
        self.assertEqual([e[0] for e in evs], [0, 100, 200])


def _write_trace(rows):
    import json

    handle = tempfile.NamedTemporaryFile("w", suffix=".jsonl", delete=False)
    for row in rows:
        handle.write(json.dumps(row) + "\n")
    handle.close()
    return handle.name


if __name__ == "__main__":
    unittest.main()
