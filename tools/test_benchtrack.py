#!/usr/bin/env python3
"""Tests for the comparison and history logic in benchtrack.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import benchtrack as bt  # noqa: E402

MANIFEST = {
    "defaults": {"tolerance": {"ns": 0.20, "bytes": 0.10}, "floor_ns": 2000},
    "suites": [{"name": "s", "binary": "x"}, {"name": "wide", "binary": "y", "tolerance": {"ns": 0.50}}],
}


def doc(results, cpu="cpu", optimized=True, date="2026-01-01T00:00:00Z", suite="s"):
    return {
        "meta": {"suite": suite, "cpu": cpu, "optimized": optimized, "date": date, "git": {"commit": "abc"}},
        "results": [{"name": n, "median_ns": v, **({"unit": u} if u else {})} for n, v, u in results],
    }


def verdicts(rows):
    return {r[0]: r[5] for r in rows}


class CompareTest(unittest.TestCase):
    def test_within_tolerance(self):
        rows = bt.compare_suite(MANIFEST, "s", doc([("a", 1e6, None)]), doc([("a", 1.1e6, None)]))
        self.assertEqual(verdicts(rows), {"a": ""})

    def test_slower_and_faster(self):
        before = doc([("a", 1e6, "ns"), ("b", 1e6, "ns")])
        after = doc([("a", 1.5e6, "ns"), ("b", 0.5e6, "ns")])
        self.assertEqual(verdicts(bt.compare_suite(MANIFEST, "s", before, after)), {"a": "slower", "b": "faster"})

    def test_missing_unit_means_ns(self):
        rows = bt.compare_suite(MANIFEST, "s", doc([("a", 1e6, None)]), doc([("a", 2e6, None)]))
        self.assertEqual(rows[0][1], "ns")
        self.assertEqual(rows[0][5], "slower")

    def test_noise_floor(self):
        rows = bt.compare_suite(MANIFEST, "s", doc([("a", 100, "ns")]), doc([("a", 1000, "ns")]))
        self.assertEqual(verdicts(rows), {"a": ""})  # 10x, but under 2 us

    def test_floor_does_not_apply_to_bytes(self):
        rows = bt.compare_suite(MANIFEST, "s", doc([("m", 1000, "bytes")]), doc([("m", 1500, "bytes")]))
        self.assertEqual(verdicts(rows), {"m": "slower"})

    def test_counts_not_compared(self):
        rows = bt.compare_suite(MANIFEST, "s", doc([("n", 10, "count")]), doc([("n", 1000, "count")]))
        self.assertEqual(verdicts(rows), {"n": ""})

    def test_suite_tolerance_override(self):
        before, after = doc([("a", 1e6, "ns")]), doc([("a", 1.4e6, "ns")])
        self.assertEqual(verdicts(bt.compare_suite(MANIFEST, "s", before, after)), {"a": "slower"})
        self.assertEqual(verdicts(bt.compare_suite(MANIFEST, "wide", before, after)), {"a": ""})

    def test_new_and_removed(self):
        rows = bt.compare_suite(MANIFEST, "s", doc([("old", 1e6, None)]), doc([("fresh", 1e6, None)]))
        self.assertEqual(verdicts(rows), {"fresh": "new", "old": "removed"})

    def test_zero_baseline(self):
        rows = bt.compare_suite(MANIFEST, "s", doc([("a", 0, "ns")]), doc([("a", 0, "ns")]))
        self.assertEqual(verdicts(rows), {"a": ""})


class HistoryTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self._orig = bt.HISTORY
        bt.HISTORY = Path(self.tmp.name)
        self.addCleanup(lambda: setattr(bt, "HISTORY", self._orig))

    def write(self, name, **kw):
        d = bt.HISTORY / name
        d.mkdir()
        (d / "s.json").write_text(json.dumps(doc([("a", 1e6, None)], **kw)))
        return d

    def test_runs_sorted_by_date(self):
        b = self.write("zzz", date="2026-01-01T00:00:00Z")
        a = self.write("aaa", date="2026-01-02T00:00:00Z")
        self.assertEqual(bt.list_runs(), [b, a])

    def test_same_machine(self):
        a = self.write("a", cpu="x")
        b = self.write("b", cpu="x")
        c = self.write("c", cpu="y")
        d = self.write("d", cpu="x", optimized=False)
        self.assertTrue(bt.same_machine(a, b))
        self.assertFalse(bt.same_machine(a, c))
        self.assertFalse(bt.same_machine(a, d))

    def test_load_single_file(self):
        d = self.write("a")
        self.assertEqual(list(bt.load_run(d / "s.json")), ["s"])


if __name__ == "__main__":
    unittest.main()
