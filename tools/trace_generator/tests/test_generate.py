import json
import os
import tempfile
import unittest

from tools.trace_generator import generate

BASE = {
    "workload": "test",
    "db": "uniref90",
    "num_blocks": 64,
    "num_shards": 4,
    "block_bytes": 1024,
    "num_queries": 8,
    "arrival": {"type": "exponential", "mean_ns": 1000},
    "scan": {"block_interval_ns": 100, "jitter_cv": 0.0},
}


def config_with(**overrides):
    cfg = json.loads(json.dumps(BASE))
    cfg.update(overrides)
    return cfg


def query_rows(rows):
    return [r for r in rows if r.get("type") == "query"]


def all_blocks_of(row):
    out = set()
    for stream in row["streams"]:
        out.update(stream["blocks"])
    return out


class TestConfigLine(unittest.TestCase):
    def test_config_line_is_first_and_matches_config(self):
        cfg = config_with(shard_policy="all_shared")
        rows = generate.generate_trace(cfg, seed=7)
        head = rows[0]
        self.assertEqual(head["type"], "config")
        self.assertEqual(head["format_version"], 1)
        self.assertEqual(head["num_blocks"], cfg["num_blocks"])
        self.assertEqual(head["num_shards"], cfg["num_shards"])
        self.assertEqual(head["block_bytes"], cfg["block_bytes"])
        self.assertEqual(head["seed"], 7)

    def test_query_count(self):
        cfg = config_with(shard_policy="all_shared")
        rows = generate.generate_trace(cfg, seed=1)
        self.assertEqual(len(query_rows(rows)), cfg["num_queries"])


class TestDeterminism(unittest.TestCase):
    def test_same_seed_same_rows(self):
        cfg = config_with(shard_policy="prefix_then_split")
        a = generate.generate_trace(cfg, seed=3)
        b = generate.generate_trace(cfg, seed=3)
        self.assertEqual(a, b)

    def test_write_trace_is_byte_identical(self):
        cfg = config_with(shard_policy="random_windows")
        with tempfile.TemporaryDirectory() as tmp:
            p1 = os.path.join(tmp, "a.jsonl")
            p2 = os.path.join(tmp, "b.jsonl")
            generate.write_trace(generate.generate_trace(cfg, seed=5), p1)
            generate.write_trace(generate.generate_trace(cfg, seed=5), p2)
            with open(p1, "rb") as f1, open(p2, "rb") as f2:
                self.assertEqual(f1.read(), f2.read())


class TestStreamShape(unittest.TestCase):
    def test_block_times_match_interval(self):
        cfg = config_with(shard_policy="all_shared")
        for row in query_rows(generate.generate_trace(cfg, seed=11)):
            for stream in row["streams"]:
                start = stream["first_block_ns"]
                step = stream["block_interval_ns"]
                times = [start + i * step for i in range(len(stream["blocks"]))]
                self.assertTrue(all(a < b for a, b in zip(times, times[1:])))
                self.assertEqual(len(times), len(stream["blocks"]))

    def test_streams_start_at_arrival(self):
        cfg = config_with(shard_policy="all_shared")
        for row in query_rows(generate.generate_trace(cfg, seed=2)):
            for stream in row["streams"]:
                self.assertEqual(stream["first_block_ns"], row["arrival_ns"])

    def test_arrivals_non_decreasing(self):
        cfg = config_with(shard_policy="all_shared")
        arrivals = [r["arrival_ns"] for r in query_rows(generate.generate_trace(cfg, seed=4))]
        self.assertEqual(arrivals, sorted(arrivals))


class TestPolicies(unittest.TestCase):
    def test_all_shared_touches_every_block(self):
        cfg = config_with(shard_policy="all_shared")
        rows = query_rows(generate.generate_trace(cfg, seed=6))
        sets = [all_blocks_of(r) for r in rows]
        self.assertEqual(sets[0], set(range(cfg["num_blocks"])))
        self.assertTrue(all(s == sets[0] for s in sets))

    def test_disjoint_groups_have_no_overlap(self):
        cfg = config_with(shard_policy="disjoint_groups")
        rows = query_rows(generate.generate_trace(cfg, seed=8))
        sets = [all_blocks_of(r) for r in rows]
        union = set()
        for s in sets:
            self.assertFalse(union & s, "disjoint_groups produced overlapping blocks")
            union |= s
        self.assertEqual(union, set(range(cfg["num_blocks"])))

    def test_random_windows_respect_fraction(self):
        cfg = config_with(shard_policy="random_windows")
        cfg["shard_policy_params"] = {"window_fraction": 0.25}
        rows = query_rows(generate.generate_trace(cfg, seed=9))
        expected = round(0.25 * cfg["num_blocks"])
        for row in rows:
            self.assertEqual(len(all_blocks_of(row)), expected)


class TestConfigFiles(unittest.TestCase):
    def test_shipped_configs_load_and_generate(self):
        cfg_dir = os.path.join(os.path.dirname(__file__), "..", "configs")
        for name in ("w1", "w2", "w3", "w4"):
            path = os.path.join(cfg_dir, f"{name}.json")
            cfg = generate.load_config(path)
            rows = generate.generate_trace(cfg, seed=1)
            self.assertEqual(len(query_rows(rows)), cfg["num_queries"])


if __name__ == "__main__":
    unittest.main()
