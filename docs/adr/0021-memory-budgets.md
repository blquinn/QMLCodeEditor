# 0021. Memory budgets: bound what grows with the document or the history

- Status: Accepted
- Date: 2026-10-09

## Context

PERF-03 asked where the memory goes. The 100 MB benchmark file (`bench_core`, `memory/*`, `bench_syntax` `memory/*`, `bench_scroll` `memory/*`; recorded in `benchmarks/history/2026-10-09-ae4a0d74a3`) says:

| What | Measured | Verdict |
|---|---|---|
| Rope | 2.16 MB of heap per million units; the text itself is 2.00 MB | 8% over the text (5.4% node objects and child arrays, the rest malloc). Not worth changing: dropping the empty `std::vector` every leaf carries would save 1.2%. |
| Parse tree (tree-sitter) | 33–54 bytes per source unit for JSON, JavaScript, HTML and XML; **146** for Markdown | 16 M units, the old limit for a whole-document parse, held 0.5–0.9 GB of tree (2.3 GB for Markdown) next to 32 MB of text. By far the largest consumer. |
| Undo history | 165 bytes per typed character | A one-leaf rope and an edit record per keystroke, for as long as the history lives (unlimited by default). |
| Layout cache | about 4 KB per layout; capped by count (256, or 3 × the rows of the plan) | Fine for short lines. A window of a very long line (ADR 0020) is thousands of units, so the same count could hold hundreds of megabytes. |

Everything else is bounded by the viewport or by the number of lines times a few bytes (`WrapMap`: 4 bytes a line, and only while wrap is on).

## Decision

- **A parse memory budget.** `TreeSitterHighlighter::parseMemoryBudget` (default 512 MiB, 0 for none) is divided by the language's `treeBytesPerUnit` (`LanguageInfo`, seeded from `bench_syntax`; 64 for a host's own language) and the smaller of that and `fullParseLimit` is the largest document that is parsed whole (`effectiveFullParseLimit()`). Larger ones keep a window tree, as they always did above the limit. Defaults now cap a whole parse at about 12 M units of JSON, 10 M of JavaScript, 3.7 M of Markdown; the tree stays near 512 MB at most.
- **Runs of typing and deleting are one undo record.** A step that continues the previous edit (the rule that already decided whether to merge steps) now extends the previous record instead of adding one: `inserted` grows, or `removed` grows at either end. 165 bytes per keystroke become 6. Groups of several edits (several cursors) still append; they are not common enough in long runs to be worth the bookkeeping.
- **The layout cache has a byte cap** (32 MB) as well as a count. Every layout is charged an estimate (its text twice plus 48 bytes a unit of shaping state, calibrated on the heap growth of a scroll). Eviction is from the old end and always keeps the newest layout. `RenderStats::layoutBytes` shows it.
- **Not done:** trimming rope nodes (see the table), a byte limit on undo history (`setLimit` counts steps; with typing merged, the remaining growth is the edited text itself), and memory-mapping untouched regions (PERF-02, deferred).

## Consequences

- The cost of a very large Markdown or XML file moves from memory to the first window of highlights: above the budget the highlighter parses around the viewport, so highlighting follows scrolling with a short delay instead of being complete.
- Hosts that register languages should set `LanguageInfo::treeBytesPerUnit` from a measurement of their grammar (`bench_syntax` `memory/*` shows how); the default of 64 is conservative for the built-ins.
- The estimates for layouts are approximate (they were about twice the heap growth measured on the 2 MB scroll file, which started with a partly full cache); they only have to be proportional for the cap to bound what it should.
