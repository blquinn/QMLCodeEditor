#!/usr/bin/env python3
"""Progress report and validator for ROADMAP.md (stdlib only).

    python3 tools/roadmap.py          # milestone progress table
    python3 tools/roadmap.py --check  # validate; non-zero exit on problems
    python3 tools/roadmap.py --next   # list the next todo item per milestone
"""

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

ROADMAP = Path(__file__).resolve().parent.parent / "ROADMAP.md"

AREAS = {
    "INFRA", "CORE", "RENDER", "INPUT", "WRAP", "GUTTER", "MULTI",
    "SYNTAX", "FOLD", "DIAG", "VIM", "API", "PERF",
}
STATUS = {" ": "todo", "~": "doing", "x": "done", "-": "dropped"}

MILESTONE_RE = re.compile(r"^## (M\d+) — (.+?)\s*$")
ITEM_RE = re.compile(r"^- \[(.)\] \*\*([A-Z]+)-(\d{2,3})\*\* (.+?)\s*$")
LOOSE_ITEM_RE = re.compile(r"^\s*- \[.?\]")
DONE_RE = re.compile(r"— done \d{4}-\d{2}-\d{2}(?: \([0-9a-f]{4,40}\))?\s*$")


class Item:
    def __init__(self, milestone, status, area, num, text, line):
        self.milestone = milestone
        self.status = status
        self.area = area
        self.num = num
        self.text = text
        self.line = line

    @property
    def id(self):
        return f"{self.area}-{self.num}"


def parse(path):
    problems = []
    milestones = {}  # id -> title, insertion ordered
    items = []
    current = None
    in_fence = False

    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if raw.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue

        m = MILESTONE_RE.match(raw)
        if m:
            current = m.group(1)
            if current in milestones:
                problems.append(f"line {lineno}: duplicate milestone {current}")
            milestones[current] = m.group(2)
            continue

        m = ITEM_RE.match(raw)
        if m:
            mark, area, num, text = m.groups()
            if mark not in STATUS:
                problems.append(f"line {lineno}: unknown status marker [{mark}]")
                continue
            if current is None:
                problems.append(f"line {lineno}: item outside any milestone")
            items.append(Item(current, STATUS[mark], area, num, text, lineno))
        elif LOOSE_ITEM_RE.match(raw) and not raw.startswith(" "):
            # Top-level checklist line that doesn't fit the item format.
            problems.append(f"line {lineno}: malformed item: {raw.strip()[:60]}")

    return milestones, items, problems


def validate(milestones, items, problems):
    seen = {}
    for it in items:
        if it.area not in AREAS:
            problems.append(f"line {it.line}: unknown area prefix {it.area} in {it.id}")
        if it.id in seen:
            problems.append(
                f"line {it.line}: duplicate ID {it.id} (first at line {seen[it.id]})"
            )
        seen[it.id] = it.line
        if it.status == "done" and not DONE_RE.search(it.text):
            problems.append(
                f"line {it.line}: {it.id} is done but lacks '— done YYYY-MM-DD'"
            )
        if it.status != "done" and DONE_RE.search(it.text):
            problems.append(f"line {it.line}: {it.id} has a done date but is not [x]")
        if it.status == "dropped" and "dropped" not in it.text.lower():
            problems.append(
                f"line {it.line}: {it.id} is dropped; say 'dropped: <reason>' in the line"
            )
    for mid in milestones:
        if not any(it.milestone == mid for it in items):
            problems.append(f"milestone {mid} has no items")
    return problems


def report(milestones, items):
    rows = []
    for mid, title in milestones.items():
        mine = [it for it in items if it.milestone == mid]
        c = Counter(it.status for it in mine)
        total = len(mine)
        finished = c["done"] + c["dropped"]
        nxt = next((it for it in mine if it.status in ("doing", "todo")), None)
        rows.append(
            (mid, title, f"{finished}/{total}", str(c["doing"]), nxt.id if nxt else "-")
        )

    headers = ("", "Milestone", "Done", "Doing", "Next")
    widths = [max(len(r[i]) for r in rows + [headers]) for i in range(5)]
    fmt = "  ".join(f"{{:<{w}}}" for w in widths)
    print(fmt.format(*headers))
    print(fmt.format(*("-" * w for w in widths)))
    for r in rows:
        print(fmt.format(*r))

    total = len(items)
    finished = sum(1 for it in items if it.status in ("done", "dropped"))
    pct = 100 * finished // total if total else 0
    print(f"\n{finished}/{total} items complete ({pct}%)")


def show_next(milestones, items):
    for mid, title in milestones.items():
        nxt = next(
            (it for it in items if it.milestone == mid and it.status in ("doing", "todo")),
            None,
        )
        if nxt:
            mark = "~" if nxt.status == "doing" else " "
            print(f"{mid} {title}: [{mark}] {nxt.id} {nxt.text}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="validate ROADMAP.md")
    ap.add_argument("--next", action="store_true", help="show next todo per milestone")
    ap.add_argument("--file", type=Path, default=ROADMAP, help="roadmap path")
    args = ap.parse_args()

    milestones, items, problems = parse(args.file)
    validate(milestones, items, problems)

    if args.check:
        if problems:
            print("\n".join(problems), file=sys.stderr)
            return 1
        print(f"ok: {len(milestones)} milestones, {len(items)} items")
        return 0

    if problems:
        print("warning: roadmap has problems (run with --check):", file=sys.stderr)
        print("\n".join(f"  {p}" for p in problems), file=sys.stderr)
    if args.next:
        show_next(milestones, items)
    else:
        report(milestones, items)
    return 0


if __name__ == "__main__":
    sys.exit(main())
