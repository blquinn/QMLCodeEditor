#!/usr/bin/env python3
"""Record benchmark runs and compare them over time (stdlib only). Informational: never fails on a change.

    python3 tools/benchtrack.py record --build-dir build/release [--note TEXT]
    python3 tools/benchtrack.py compare [A] [B]      # history dirs or single .json files; default: last two runs here
    python3 tools/benchtrack.py history [REGEX] [--suite S]
    python3 tools/benchtrack.py list

Tracked suites live in benchmarks/tracked.json; runs are stored in benchmarks/history/<date>-<hash>[-N]/<suite>.json.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "benchmarks" / "tracked.json"
HISTORY = ROOT / "benchmarks" / "history"


# ---- manifest and tolerances -------------------------------------------------------------------------------------

def load_manifest(path=MANIFEST):
    with open(path) as f:
        return json.load(f)


def tolerance_for(manifest, suite, unit):
    """Fractional change above which a case counts as notable, or None when the unit isn't compared."""
    tol = dict(manifest.get("defaults", {}).get("tolerance", {}))
    for s in manifest.get("suites", []):
        if s["name"] == suite:
            tol.update(s.get("tolerance", {}))
    return tol.get(unit)


# ---- comparison --------------------------------------------------------------------------------------------------

def index_results(doc):
    """{case name: (median, unit)} for one result file."""
    return {r["name"]: (float(r["median_ns"]), r.get("unit", "ns")) for r in doc.get("results", [])}


def compare_suite(manifest, suite, before, after):
    """Rows for one suite: (name, unit, before, after, delta or None, verdict).

    verdict is 'slower', 'faster', '' (within tolerance / not compared), 'new' or 'removed'. For 'ns' cases a change
    smaller than the noise floor is never notable.
    """
    floor = float(manifest.get("defaults", {}).get("floor_ns", 2000))
    b, a = index_results(before), index_results(after)
    rows = []
    for name in sorted(set(b) | set(a)):
        if name not in b:
            rows.append((name, a[name][1], None, a[name][0], None, "new"))
            continue
        if name not in a:
            rows.append((name, b[name][1], b[name][0], None, None, "removed"))
            continue
        (bv, unit), (av, _) = b[name], a[name]
        delta = (av - bv) / bv if bv else (0.0 if av == 0 else float("inf"))
        tol = tolerance_for(manifest, suite, unit)
        verdict = ""
        if tol is not None and abs(delta) > tol and not (unit == "ns" and abs(av - bv) < floor):
            verdict = "slower" if av > bv else "faster"
        rows.append((name, unit, bv, av, delta, verdict))
    return rows


def fmt_value(v, unit):
    if v is None:
        return "-"
    if unit == "ns":
        if v < 1e3:
            return f"{v:.1f} ns"
        if v < 1e6:
            return f"{v / 1e3:.2f} us"
        if v < 1e9:
            return f"{v / 1e6:.2f} ms"
        return f"{v / 1e9:.3f} s"
    return f"{v:,.0f} {unit}"


def print_rows(suite, rows, show_all=False):
    shown = [r for r in rows if show_all or r[5]]
    notable = sum(1 for r in rows if r[5] in ("slower", "faster"))
    print(f"{suite}: {len(rows)} cases, {notable} notable change(s)")
    if not shown:
        return
    width = max(len(r[0]) for r in shown)
    for name, unit, bv, av, delta, verdict in shown:
        d = "" if delta is None else f"{delta * 100:+.1f}%"
        print(f"  {name:<{width}}  {fmt_value(bv, unit):>12}  {fmt_value(av, unit):>12}  {d:>8}  {verdict}")


# ---- history on disk ---------------------------------------------------------------------------------------------

def load_run(path):
    """{suite: doc} from a history dir or a single result file."""
    path = Path(path)
    if path.is_dir():
        docs = {}
        for f in sorted(path.glob("*.json")):
            doc = json.loads(f.read_text())
            docs[doc.get("meta", {}).get("suite") or f.stem] = doc
        return docs
    doc = json.loads(path.read_text())
    return {doc.get("meta", {}).get("suite") or path.stem: doc}


def run_meta(docs):
    for doc in docs.values():
        return doc.get("meta", {})
    return {}


def list_runs():
    """History dirs, oldest first (by recorded date, then name)."""
    runs = []
    if HISTORY.is_dir():
        for d in HISTORY.iterdir():
            if d.is_dir() and any(d.glob("*.json")):
                runs.append((run_meta(load_run(d)).get("date", ""), d.name, d))
    return [d for _, _, d in sorted(runs)]


def same_machine(a, b):
    ma, mb = run_meta(load_run(a)), run_meta(load_run(b))
    return ma.get("cpu") == mb.get("cpu") and ma.get("optimized") == mb.get("optimized")


def compare_runs(manifest, a, b, show_all=False):
    before, after = load_run(a), load_run(b)
    print(f"before: {a}\nafter:  {b}")
    mb, ma = run_meta(before), run_meta(after)
    if mb.get("cpu") != ma.get("cpu") or mb.get("optimized") != ma.get("optimized"):
        print("warning: the runs come from different machines or build types; differences may not be real")
    for suite in sorted(set(before) | set(after)):
        if suite not in before or suite not in after:
            print(f"{suite}: only in {'after' if suite in after else 'before'}")
            continue
        print_rows(suite, compare_suite(manifest, suite, before[suite], after[suite]), show_all)


# ---- git ---------------------------------------------------------------------------------------------------------

def git(*args):
    try:
        return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def git_info():
    return {
        "commit": git("rev-parse", "--short=10", "HEAD"),
        "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
        "dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
    }


# ---- commands ----------------------------------------------------------------------------------------------------

def cmd_record(args):
    manifest = load_manifest()
    build = Path(args.build_dir).resolve()
    info = git_info()
    stamp = time.strftime("%Y-%m-%d")
    base = f"{stamp}-{info['commit'] or 'nogit'}"
    dest, n = HISTORY / base, 1
    while dest.exists():
        n += 1
        dest = HISTORY / f"{base}-{n}"
    if info["dirty"]:
        print("warning: working tree has uncommitted changes; the recorded commit won't match the measured code")

    recorded, started = [], time.monotonic()
    with tempfile.TemporaryDirectory() as tmp:
        for suite in manifest["suites"]:
            exe = build / suite["binary"]
            if not exe.exists():
                if not suite.get("optional"):
                    sys.exit(f"missing {exe} (build it first)")
                print(f"-- {suite['name']}: not built, skipped")
                continue
            out = Path(tmp) / f"{suite['name']}.json"
            env = {**os.environ, **suite.get("env", {})}
            print(f"-- {suite['name']} ...", flush=True)
            t0 = time.monotonic()
            r = subprocess.run([str(exe), *suite.get("args", []), "--json", str(out)], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            if r.returncode != 0 or not out.exists():
                sys.exit(f"{suite['name']} failed (exit {r.returncode}):\n{r.stderr}")
            doc = json.loads(out.read_text())
            doc["meta"]["suite"] = suite["name"]
            doc["meta"]["git"] = info
            doc["meta"]["args"] = suite.get("args", [])
            if args.note:
                doc["meta"]["note"] = args.note
            if not doc["meta"].get("optimized"):
                print(f"warning: {suite['name']} is not an optimized build; timings are not meaningful")
            recorded.append((suite["name"], doc))
            print(f"   {len(doc['results'])} cases in {time.monotonic() - t0:.1f}s")
    if not recorded:
        sys.exit("nothing recorded")
    dest.mkdir(parents=True)
    for name, doc in recorded:
        (dest / f"{name}.json").write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
    print(f"recorded {len(recorded)} suite(s) in {time.monotonic() - started:.0f}s -> {dest.relative_to(ROOT)}")

    previous = [r for r in list_runs() if r != dest and same_machine(r, dest)]
    if previous:
        print()
        compare_runs(manifest, previous[-1], dest)
    return 0


def cmd_compare(args):
    manifest = load_manifest()
    if args.a and args.b:
        a, b = args.a, args.b
    else:
        runs = list_runs()
        if args.a:  # one argument: compare it with the latest run
            a, b = args.a, runs[-1] if runs else None
        elif len(runs) >= 2:
            latest = runs[-1]
            peers = [r for r in runs[:-1] if same_machine(r, latest)]
            if not peers:
                sys.exit("no earlier run from this machine to compare with")
            a, b = peers[-1], latest
        else:
            sys.exit("need two recorded runs (see `benchtrack.py record`)")
    if b is None:
        sys.exit("no recorded runs")
    compare_runs(manifest, a, b, args.all)
    return 0


def cmd_history(args):
    runs = list_runs()
    if not runs:
        sys.exit("no recorded runs")
    ref = run_meta(load_run(runs[-1]))
    pattern = re.compile(args.regex or "")
    series = {}  # (suite, name) -> [(label, value, unit)]
    for r in runs:
        docs = load_run(r)
        m = run_meta(docs)
        if m.get("cpu") != ref.get("cpu") or m.get("optimized") != ref.get("optimized"):
            continue
        label = f"{m.get('date', '')[:10]} {m.get('git', {}).get('commit', '')}"
        for suite, doc in docs.items():
            if args.suite and suite != args.suite:
                continue
            for name, (v, unit) in index_results(doc).items():
                if pattern.search(name):
                    series.setdefault((suite, name), []).append((label, v, unit))
    if not series:
        sys.exit("no matching cases")
    for (suite, name), points in sorted(series.items()):
        print(f"{suite} {name}")
        prev = None
        for label, v, unit in points:
            d = "" if prev in (None, 0) else f"{(v - prev) / prev * 100:+.1f}%"
            print(f"  {label:<24} {fmt_value(v, unit):>12}  {d:>8}")
            prev = v
    print(f"(machine: {ref.get('cpu')})")
    return 0


def cmd_list(_args):
    for r in list_runs():
        docs = load_run(r)
        m = run_meta(docs)
        g = m.get("git", {})
        flags = " dirty" if g.get("dirty") else ""
        note = f"  # {m['note']}" if m.get("note") else ""
        print(f"{r.name:<28} {len(docs):>2} suites  {g.get('branch', '?')}{flags}  {m.get('cpu', '?')}{note}")
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record", help="run the tracked suites and store the results")
    r.add_argument("--build-dir", required=True)
    r.add_argument("--note", default="")
    r.set_defaults(fn=cmd_record)
    c = sub.add_parser("compare", help="compare two recorded runs")
    c.add_argument("a", nargs="?")
    c.add_argument("b", nargs="?")
    c.add_argument("--all", action="store_true", help="show unchanged cases too")
    c.set_defaults(fn=cmd_compare)
    h = sub.add_parser("history", help="trend of matching cases across recorded runs on this machine")
    h.add_argument("regex", nargs="?")
    h.add_argument("--suite")
    h.set_defaults(fn=cmd_history)
    l = sub.add_parser("list", help="list recorded runs")
    l.set_defaults(fn=cmd_list)
    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
