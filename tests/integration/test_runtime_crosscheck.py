"""Cross-check the local storage runtime against the discrete-event simulator.

The runtime's `--backend sim` driver must reproduce the simulator's decision
metrics for the same trace/policy/DRAM budget. This asserts the shared
ReplayEngine plus the runtime driver agree end to end. The pread backend is
checked for functional correctness (reads the Block DB with no errors).

Run:
    python3 -m unittest tests.integration.test_runtime_crosscheck -v
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

BUILD_DIR = os.environ.get("MSAFLOW_BUILD_DIR", os.path.join(REPO_ROOT, "build"))
SIM = os.environ.get("MSAFLOW_SIM", os.path.join(BUILD_DIR, "simulator", "msaflow-sim"))
RUNTIME = os.environ.get("MSAFLOW_RUNTIME", os.path.join(BUILD_DIR, "runtime", "msaflow-runtime"))

from database.builder import block_db  # noqa: E402
from tools.trace_generator import generate as trace_gen  # noqa: E402

POLICIES = ("fifo", "lru", "sharing", "urgency", "msaflow-v0")
DRAM_BLOCKS = (205, 1024)


def run_json(command):
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


class RuntimeCrossCheckTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (os.path.exists(SIM) and os.path.exists(RUNTIME)):
            raise unittest.SkipTest("msaflow-sim / msaflow-runtime binaries not built")
        cls.tmp = tempfile.mkdtemp(prefix="msaflow_crosscheck_")
        cls.traces = {}
        for workload in ("w1", "w2", "w3", "w4"):
            config = trace_gen.load_config(
                os.path.join(trace_gen.CONFIG_DIR, f"{workload}.json"))
            rows = trace_gen.generate_trace(config, seed=7)
            path = os.path.join(cls.tmp, f"{workload}.jsonl")
            trace_gen.write_trace(rows, path)
            cls.traces[workload] = path

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def _sim_args(self, trace, policy, dram):
        return [
            SIM, "--trace", trace, "--policy", policy,
            "--dram-blocks", str(dram), "--block-bytes", "134217728",
            "--bandwidth-gbps", "3", "--io-depth", "32",
            "--starvation-threshold-ms", "60000",
        ]

    def _runtime_args(self, trace, policy, dram):
        return [
            RUNTIME, "--trace", trace, "--backend", "sim", "--policy", policy,
            "--dram-blocks", str(dram), "--block-bytes", "134217728",
            "--bandwidth-gbps", "3", "--io-depth", "32",
            "--starvation-threshold-ms", "60000",
        ]

    def test_sim_backend_matches_simulator(self):
        cases = 0
        for workload, trace in self.traces.items():
            for policy in POLICIES:
                for dram in DRAM_BLOCKS:
                    sim = run_json(self._sim_args(trace, policy, dram))
                    rt = run_json(self._runtime_args(trace, policy, dram))
                    self.assertEqual(sim["config"], rt["config"])
                    self.assertEqual(
                        sim["metrics"], rt["metrics"],
                        msg=f"{workload}/{policy}/dram{dram} metrics differ")
                    cases += 1
        self.assertGreaterEqual(cases, 40)

    def test_pread_backend_reads_block_db(self):
        sequences = [
            block_db.SequenceRecord(0, "ACDEFGHIK"),
            block_db.SequenceRecord(1, "LMNPQRST"),
            block_db.SequenceRecord(2, "VWYACDE"),
        ]
        db_dir = os.path.join(self.tmp, "blockdb")
        block_db.build_block_db(sequences, db_dir, target_block_bytes=15)
        block_db.verify_block_db(db_dir)

        trace_path = os.path.join(self.tmp, "tiny_db.jsonl")
        with open(trace_path, "w", encoding="utf-8") as handle:
            for query_id in (1, 2):
                handle.write(json.dumps(
                    {"query_id": query_id, "db": "synth", "blocks": [0, 1]}) + "\n")

        for extra in ([], ["--direct"]):
            result = run_json([
                RUNTIME, "--trace", trace_path, "--db", db_dir, "--backend", "pread",
                "--policy", "msaflow-v0", "--dram-blocks", "8", "--io-depth", "4",
                *extra,
            ])
            self.assertEqual(result["storage"]["read_errors"], 0)
            self.assertGreater(result["storage"]["physical_bytes_read"], 0)
            self.assertGreater(result["metrics"]["physical_block_reads"], 0)


if __name__ == "__main__":
    unittest.main()
