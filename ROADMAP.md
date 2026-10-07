# QMLCodeEditor Roadmap

A high-performance code editor widget for QML (Qt 6). It is built directly on scene-graph primitives rather than `TextEdit`/`QQuickPaintedItem`, and is designed for very large and complex documents.

**Feature set:** gutters, line numbers, soft wrap (and no-wrap), tree-sitter syntax highlighting, code folding, multi-cursor, vim mode, and diagnostics/popups. Also a demo app, benchmark suite, unit tests, and an LSP-ready API.

Architecture decisions live in [`docs/adr/`](docs/adr/README.md). Progress: `python3 tools/roadmap.py`.

## How this file works

- Every item is a single checklist line with a stable ID, e.g. `- [ ] **CORE-03** Title`. Acceptance criteria go in indented bullets underneath.
- Markers: `[ ]` todo · `[~]` in progress · `[x]` done · `[-]` dropped (say why in the line).
- When finishing an item, mark it `[x]` and append `— done YYYY-MM-DD (shorthash)`. The hash is optional; the date is required.
- IDs are **never renumbered or reused**. A new item takes the next free number in its area, even if it lives in an earlier milestone.
- Commit messages reference IDs: `CORE-03: add rope snapshots`.
- A milestone is done when all its items are `[x]`/`[-]` **and** its exit criteria have been demonstrated.
- `python3 tools/roadmap.py` shows the progress table; `--check` validates the file (used in CI).
- Area prefixes: `INFRA CORE RENDER INPUT WRAP GUTTER MULTI SYNTAX FOLD DIAG VIM API PERF`.

## Performance targets

These are the bar every milestone is measured against; benchmarks (INFRA-08, PERF-05) track them.

| Scenario | Target |
|---|---|
| Open a 100 MB / 2M-line file | first frame < 500 ms (content loads progressively) |
| Keystroke to frame | < 8 ms |
| Scrolling with highlighting on | steady 120 fps |
| Toggle wrap on a 1M-line file | stays interactive (viewport re-wrapped first) |
| 5 MB single-line (minified) file | opens and scrolls without stalling |

## Milestone overview

| # | Milestone | Area |
|---|---|---|
| M0 | Foundation | INFRA |
| M1 | Text core | CORE |
| M2 | Rendering MVP | RENDER |
| M3 | Editing & input | INPUT |
| M4 | Display map & soft wrap | WRAP |
| M5 | Gutters & line numbers | GUTTER |
| M6 | Syntax highlighting | SYNTAX |
| M7 | Code folding | FOLD |
| M8 | Multi-cursor | MULTI |
| M9 | Diagnostics & decorations | DIAG |
| M10 | Vim mode | VIM |
| M11 | LSP-ready API & polish | API |
| M12 | Performance hardening | PERF |

Order is deliberate: wrap (M4) comes before folding (M7), and the display-map layering ([ADR 0004](docs/adr/0004-display-map.md)) means folding slots in later without reworking anything. Gutters (M5) need the display map to know which rows are continuations.

---

## M0 — Foundation

Get a clean, buildable project skeleton before any editor code. See [ADR 0003](docs/adr/0003-module-layout.md).

**Exit criteria:** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` works from a clean checkout; the demo app launches an empty window; the benchmark harness runs a no-op benchmark and prints timings.

- [x] **INFRA-01** Fix QML module URI mismatch — done 2026-10-02 (d94a2e0)
  - `qmldir` declares `me.blq.qmlcodeeditor` but CMake declares `QMLCodeEditor`. Pick one URI, and delete the hand-written `qmldir` (let `qt_add_qml_module` generate it).
- [x] **INFRA-02** Clean up scaffold CMake — done 2026-10-02 (ac8160e)
  - Remove the leftover `MACOSX_BUNDLE`/`WIN32_EXECUTABLE` properties (this is a library, not an app) and the placeholder `CodeEditorControls.qml` / red-rect `paint()`.
- [x] **INFRA-03** Set minimum Qt to 6.8 LTS — done 2026-10-02 (e354a59)
  - `find_package(Qt6 6.8 ...)`; document that development and CI use 6.11. Required for the public `QSGTextNode` API (6.7+) ([ADR 0001](docs/adr/0001-scene-graph-rendering.md)).
- [x] **INFRA-04** Source layout: `src/core`, `src/quick`, `src/syntax`, `demo/`, `tests/`, `benchmarks/`, `third_party/` — done 2026-10-02 (4c3b9df)
  - `core` links only Qt Core/Gui and must build and test without a window system.
- [x] **INFRA-05** `CMakePresets.json` — done 2026-10-02 (42ce444)
  - Presets: `debug`, `release`, `asan-ubsan`, `tsan`; matching build and test presets. Tests run with `QT_QPA_PLATFORM=offscreen`.
- [x] **INFRA-06** Formatting and warnings — done 2026-10-02 (a537f81)
  - `.clang-format`, a strict warnings set, and an opt-in `-Werror` option.
- [x] **INFRA-07** Qt Test harness wired into `ctest` — done 2026-10-02 (619a463)
  - One trivial passing test per module so the plumbing is proven.
- [x] **INFRA-08** Benchmark harness skeleton — done 2026-10-02 (aa44c63)
  - Qt `QBENCHMARK` or a small custom runner; JSON output for tracking over time; helper to generate large synthetic files (many short lines, few long lines, one giant line). A frame-timing hook that records per-frame render and sync cost from a `QQuickWindow`.
- [x] **INFRA-09** Demo app skeleton — done 2026-10-02 (89fabd4)
  - Standalone QML app in `demo/` that links the module and shows an empty editor item.
- [x] **INFRA-10** `tools/roadmap.py --check` run in CI/pre-commit — done 2026-10-02 (b69544c)

## M1 — Text core

The rope, anchors, change events, undo and file I/O. Pure Qt Core, fully unit-tested. See [ADR 0002](docs/adr/0002-rope-text-storage.md).

**Exit criteria:** load a 100 MB file in under 500 ms to first usable snapshot; 1M random edits keep line/column lookups under 1 µs median; property tests against a `QString` reference model pass; undo/redo round-trips arbitrary edit sequences.

- [x] **CORE-01** Persistent B-tree rope of UTF-16 chunks — done 2026-10-02 (2dc81d8)
  - Immutable, refcounted nodes; ~1–4 KB leaves; each node summarizes UTF-16 length and newline count. Insert, delete, replace and slice are O(log n).
- [x] **CORE-02** O(1) snapshots — done 2026-10-02 (d200fe5)
  - Cheap read-only copies safe to use from other threads (tree-sitter, search).
- [x] **CORE-03** Position conversions — done 2026-10-02 (8bf2fe5)
  - offset ↔ (line, column) in O(log n); line start/length queries; surrogate pairs never split by the API.
- [x] **CORE-04** Grapheme and word boundaries — done 2026-10-02 (14c7820)
  - Next/previous grapheme (via `QTextBoundaryFinder`, with an ASCII fast path) and word-boundary helpers used by movement and vim.
- [x] **CORE-05** Change events — done 2026-10-02 (5f98032)
  - Each edit emits old range, new range and inserted text length, shaped to feed both tree-sitter `TSInputEdit` and LSP `TextDocumentContentChangeEvent` without translation.
- [x] **CORE-06** Anchors — done 2026-10-02 (3dff7dc)
  - Offsets that track edits with configurable left/right gravity; the basis for diagnostics, marks, folds and cursors ([ADR 0006](docs/adr/0006-anchored-decorations.md)).
- [x] **CORE-07** Undo/redo — done 2026-10-02 (3178797)
  - Transactions with grouping rules; restores the selection set before/after.
- [x] **CORE-08** File loading — done 2026-10-02 (c4e1c5d)
  - mmap, encoding detection (UTF-8 / UTF-16 / BOM), line-ending detection with CRLF preserved; progressive load so the first snapshot is usable before the whole file is read.
- [x] **CORE-09** File saving — done 2026-10-02 (7011e80)
  - Atomic write; original line endings and encoding preserved.
- [x] **CORE-10** Tests and benchmarks for the above — done 2026-10-02 (6445d89)
  - Randomized differential tests against a `QString` model; load and edit benchmarks in the harness.

## M2 — Rendering MVP

A fast read-only-ish editor item on the scene graph. See [ADR 0001](docs/adr/0001-scene-graph-rendering.md).

**Exit criteria:** a 100 MB file scrolls at a steady 120 fps in the demo app with only visible lines laid out; frame-time benchmark committed; memory use is independent of file size beyond the rope itself.

- [x] **RENDER-01** `CodeEditor` as a `QQuickItem` using `updatePaintNode` — done 2026-10-02 (c958c32)
  - Replace the `QQuickPaintedItem` scaffold; `ItemHasContents` set; all scene-graph mutation on the render thread in `updatePaintNode`.
- [x] **RENDER-02** Scroll model — done 2026-10-02 (29b22d2)
  - Owned `contentX/contentY/contentWidth/contentHeight` properties so a QML `ScrollBar` can bind to them; kinetic scrolling friendly; scrolling changes a transform only, with no relayout.
- [x] **RENDER-03** Viewport virtualization with an LRU line-layout cache — done 2026-10-02 (c20f0f4)
  - Only lines intersecting the viewport (plus a small margin) are laid out; cache keyed by line and invalidated on edit.
- [x] **RENDER-04** Pooled `QSGTextNode`s fed by per-line `QTextLayout` — done 2026-10-02 (28dd27b)
  - Line nodes are reused as lines scroll in and out; no per-frame allocation in steady-state scrolling.
- [x] **RENDER-05** Cursor and selection geometry — done 2026-10-02 (97f63ad)
  - Batched geometry nodes behind and above the text; cursor blink timer.
- [x] **RENDER-06** Theme object — done 2026-10-02 (beb5f5e)
  - Colors for text, background, selection, cursor, current line; token-style table used by highlighting; exposed to QML for light/dark switching.
- [x] **RENDER-07** Font metrics with a monospace fast path — done 2026-10-02 (f2bfa7d)
  - Cell-grid math for ASCII/monospace runs; falls back to `QTextLayout` measurement for wide chars, emoji, ligatures and mixed fonts ([ADR 0001](docs/adr/0001-scene-graph-rendering.md)).
- [x] **RENDER-08** Tabs and whitespace — done 2026-10-02 (fee0f0d)
  - Tab stops, optional visible whitespace, configurable tab width.
- [x] **RENDER-09** `Highlighter` interface with a no-op implementation — done 2026-10-02 (afcbc5a)
  - Produces per-line format ranges for a requested line range; the tree-sitter implementation in M6 plugs in here.
- [x] **RENDER-10** Demo app opens files — done 2026-10-02 (1c469e0)
  - File dialog / command-line path / drag-and-drop in `demo/`.
- [x] **RENDER-11** Scroll and frame-time benchmarks — done 2026-10-02 (63f0522)
  - Scripted scroll over the generated large files; results recorded in JSON.
- [x] **RENDER-12** Text render type — done 2026-10-05 (bf9ff83)
  - `renderType` property (Qt/Native/Curve) applied to every text node; unset follows `QQuickWindow::textRenderType()`.

## M3 — Editing & input

Make it a real editor. Introduces the command/selection model that multi-cursor and vim build on. See [ADR 0005](docs/adr/0005-commands-and-selections.md).

**Exit criteria:** type, delete, paste, undo/redo, select with mouse and keyboard, and compose text via IME in the demo app; keystroke-to-frame under 8 ms on a 100 MB file.

- [x] **INPUT-01** `SelectionSet` and command layer — done 2026-10-02 (2a2495d)
  - Every mutation is a command applied to a selection set that always exists (a single cursor is a set of one) ([ADR 0005](docs/adr/0005-commands-and-selections.md)).
- [x] **INPUT-02** Swappable `InputHandler` interface plus the default handler — done 2026-10-02 (492badd)
  - Maps key events to commands; vim (M10) is a second implementation.
- [x] **INPUT-03** Cursor movement — done 2026-10-02 (492badd)
  - Char, word, line start/end, document start/end, page up/down; grapheme-aware.
- [x] **INPUT-04** Mouse input — done 2026-10-02 (c2f42e9)
  - Click, drag-select, double-click word, triple-click line, shift-click extend.
- [x] **INPUT-05** Input method (IME) support — done 2026-10-02 (a916a32)
  - `inputMethodQuery` / `inputMethodEvent` with preedit rendering and correct candidate-window placement.
- [x] **INPUT-06** Clipboard — done 2026-10-02 (c2f42e9)
  - Cut, copy, paste; selection clipboard on Linux; large pastes don't block the UI.
- [x] **INPUT-07** Scroll-to-cursor and cursor-visibility behavior — done 2026-10-02 (492badd)
- [x] **INPUT-08** Undo/redo wired to commands — done 2026-10-02 (492badd)
  - Keyboard shortcuts; typing is coalesced into sensible undo groups.
- [x] **INPUT-09** Basic auto-indent — done 2026-10-02 (b5744e2)
  - Newline keeps indentation; tab/shift-tab indent selections.
- [x] **INPUT-10** Focus handling and cursor blink — done 2026-10-02 (b2ae309)
- [x] **INPUT-11** Keystroke-to-frame benchmark — done 2026-10-02 (bd9e4d8)
  - `bench_typing`: typing, Enter, Backspace, paste and undo on the generated 100 MB file; event-to-frame latency median/p95/max; JSON result committed; ctest smoke variant.
- [x] **INPUT-12** Smart tabs — done 2026-10-05 (f4aebee)
  - Indentation (spaces vs tabs, width) is detected from the first lines of a document on load and `setText`; `detectIndentation` turns it off.
  - Backspace inside leading spaces deletes back to the previous indent stop; Shift+Tab outdents to the previous stop.
- [x] **INPUT-13** Bracket auto-pairing — done 2026-10-05 (89b0f6e)
  - Typing an opener inserts its closer (quotes only at a word boundary); typing a closer over the same closer steps past it; typing an opener over a selection wraps it.
  - Backspace between an empty pair deletes both; Enter between brackets puts the closer on the line below and the cursor on an indented line between (smart-tab unit).
  - `autoClose` (default on, every language) and `autoClosePairs` properties; works per cursor with multiple selections.

## M4 — Display map & soft wrap

Soft wrap is a core feature, not an extra. Wrapped and unwrapped modes share one pipeline. See [ADR 0004](docs/adr/0004-display-map.md).

**Exit criteria:** on a 1M-line file, toggling wrap and resizing the window never drops below interactive frame rates (viewport re-wraps first, rest in background); cursor up/down across wrapped rows keeps its goal column; no-wrap mode is unchanged in speed.

- [x] **WRAP-01** `DisplayMap` layering: buffer → `FoldMap` (identity for now) → `WrapMap` → display rows — done 2026-10-02 (3c4e4be)
  - All rendering, hit-testing and scrolling go through the map; nothing else assumes one buffer line equals one row.
- [x] **WRAP-02** `WrapMap` with a row-count summary tree — done 2026-10-02 (3126d94)
  - O(log n) buffer line ↔ display row; buffer position ↔ (row, x).
- [x] **WRAP-03** Incremental re-wrap on edit — done 2026-10-02 (2d6db3e)
  - Only affected lines are re-wrapped; row count deltas propagate through the summary tree.
- [x] **WRAP-04** Re-wrap on resize — done 2026-10-02 (b3ccdd5)
  - Viewport lines first, remainder in the background; scroll position anchored to the top visible buffer position so content doesn't jump.
- [x] **WRAP-05** Wrap modes — done 2026-10-02 (7f51a6b)
  - Off, at viewport width, at a fixed column; word-boundary vs. character wrapping.
- [x] **WRAP-06** Hanging indent option — done 2026-10-02 (72c937f)
  - Continuation rows align under the line's indentation (optionally plus an extra indent).
- [x] **WRAP-07** Visual-row cursor movement — done 2026-10-02 (950c5b3)
  - Up/down move by display row with a sticky goal X; Home/End go to row start/end (second press goes to line start/end).
- [x] **WRAP-08** Wrap toggle at runtime and horizontal scrolling in no-wrap mode — done 2026-10-02 (d674891)
- [x] **WRAP-09** Very long lines wrap without stalling — done 2026-10-02 (88235dd)
  - A 5 MB single line wraps incrementally and stays responsive.
- [x] **WRAP-10** Wrap benchmarks — done 2026-10-02 (6de2f70)
  - Wrap/unwrap/resize timing on generated large files.

## M5 — Gutters & line numbers

**Exit criteria:** line numbers, a change-marker column and a custom QML-provided column render correctly with wrap on and off; gutter width tracks digit count without jitter during scroll.

- [x] **GUTTER-01** Gutter framework — done 2026-10-02 (c752ae6)
  - Ordered columns supplied by providers (built-in or QML); each column reports its width and paints per visible row.
- [x] **GUTTER-02** Line numbers — done 2026-10-02 (c752ae6)
  - Absolute, relative or hybrid; continuation rows of wrapped lines show no number; current line highlighted.
- [x] **GUTTER-03** Auto-sizing width — done 2026-10-02 (c752ae6)
  - Based on digit count of the last line, stable while scrolling.
- [x] **GUTTER-04** Marker column API — done 2026-10-02 (c752ae6)
  - Hosts attach icons/colors per line (git change bars, breakpoints, diagnostics); batched into the scene graph.
- [x] **GUTTER-05** Gutter interaction — done 2026-10-02 (c752ae6)
  - Click/drag selects lines; click signal for hosts (e.g. toggling breakpoints).
- [x] **GUTTER-06** QML delegate column — done 2026-10-02 (c752ae6)
  - `DelegateColumn` instantiates a host-supplied delegate per visible row, pooled and reused as rows scroll; delegates never exist for rows outside the frame plan.
- [x] **GUTTER-07** Change-marker column — done 2026-10-02 (c752ae6)
  - `ChangeColumn` marks lines edited since load/save with anchored ranges; cleared on text reset and save.
- [x] **GUTTER-08** Gutter benchmark and ADR — done 2026-10-02 (a8e2e6e)
  - Scroll with the gutter on, wrap on and off, and relative numbers while the cursor moves; ADR 0012 records the design.

## M6 — Syntax highlighting

**Exit criteria:** C++, JSON, QML/JS, Python and Markdown highlight correctly (C, C++, QML and Python were dropped again in SYNTAX-14 to save binary size); typing never waits on the parser; a 100 MB file highlights the visible region without a full parse blocking the UI.

- [x] **SYNTAX-01** Vendor tree-sitter core and grammars via CMake — done 2026-10-03 (a965b9e)
  - `FetchContent` / `third_party/`: C/C++, JSON, JavaScript/QML, Python, Markdown. Pinned versions.
- [x] **SYNTAX-02** Language registry — done 2026-10-03 (a965b9e)
  - Detect language by file extension / name / shebang; load grammar plus queries.
- [x] **SYNTAX-03** Background incremental parsing over rope snapshots — done 2026-10-03 (a965b9e)
  - `TSInput` callback reads rope chunks as UTF-16 (`TSInputEncodingUTF16LE`); parser runs on a worker thread; stale results are discarded.
- [x] **SYNTAX-04** Edit propagation — done 2026-10-03 (a965b9e)
  - Core change events (CORE-05) drive `ts_tree_edit` and reparse with the old tree.
- [x] **SYNTAX-05** Visible-range highlight queries — done 2026-10-03 (a965b9e)
  - Run `highlights.scm` captures only for the lines being rendered, with a margin.
- [x] **SYNTAX-06** Capture → theme mapping — done 2026-10-03 (a965b9e)
  - Theme token styles produce `QTextLayout::FormatRange`s through the `Highlighter` interface (RENDER-09).
- [x] **SYNTAX-07** Targeted layout invalidation — done 2026-10-03 (a965b9e)
  - `ts_tree_get_changed_ranges` limits which cached line layouts get invalidated.
- [x] **SYNTAX-08** Injections — done 2026-10-03 (a965b9e)
  - JS inside QML, fenced code in Markdown.
- [x] **SYNTAX-09** Interim highlighting during parse — done 2026-10-03 (a965b9e)
  - Keep previous highlights (shifted through anchors) until the new tree lands, to avoid flicker.
- [x] **SYNTAX-10** Highlighting benchmarks — done 2026-10-03 (a965b9e)
  - Initial parse time, edit-to-highlight latency, memory per MB of source.
- [x] **SYNTAX-11** Viewport window parse for large files — done 2026-10-03 (a965b9e)
  - A worker parses a window around the viewport first; the full parse runs only below a size cap (default 32 MB). Above it, scrolling re-parses the window.
- [x] **SYNTAX-12** `SyntaxHighlighter` QML element and demo language menu — done 2026-10-03 (a965b9e)
  - Auto-detects by file name; `language` override; `CodeEditor.highlighter` settable from QML.
- [x] **SYNTAX-13** HTML and XML grammars — done 2026-10-03 (aa3162c)
  - HTML (with JavaScript in `<script>`) and XML (also SVG, Qt `.ui`/`.qrc`, plist); `<style>` content stays plain until a CSS grammar is added.
- [x] **SYNTAX-14** Drop C, C++, QML and Python grammars for now — done 2026-10-03 (083810a)
  - They made the demo 8 MB (C++ alone 3.5 MB of parse tables). Shipped languages: JSON, JavaScript, HTML, XML, Markdown. To restore one, revert the SYNTAX-14 commit and re-add its registry entry and `QceTreeSitter.cmake` fetch (pinned versions are in git history); a per-language build option is the alternative.

## M7 — Code folding

**Exit criteria (demonstrated 2026-10-03: tests tst_folding, tst_foldmap, tst_foldquery; ADR 0014 benchmarks; hands-on mouse use in a real window not done):** fold/unfold via gutter and keyboard on a large file; folds survive edits; wrap and folding compose correctly (a folded wrapped line is one row group); fold state doesn't slow scrolling.

- [x] **FOLD-01** Real `FoldMap` replacing the identity layer — done 2026-10-03 (5cc4887)
  - Hidden buffer ranges map to zero rows; O(log n) lookups ([ADR 0004](docs/adr/0004-display-map.md)).
- [x] **FOLD-02** Fold range provider from tree-sitter `folds.scm` — done 2026-10-03 (c3cb41e)
- [x] **FOLD-03** Indent-based fold fallback for languages without queries — done 2026-10-03 (6c5c00c)
- [x] **FOLD-04** Fold gutter column — done 2026-10-03 (d5c439c)
  - Chevrons on foldable lines; click toggles; hover highlights the range.
- [x] **FOLD-05** Folded-region placeholder rendering — done 2026-10-03 (d5c439c)
  - Inline "…" chip that expands on click.
- [x] **FOLD-06** Cursor/selection/edit semantics across folds — done 2026-10-03 (d5c439c)
  - Cursor entering a fold unfolds it, or skips it, by setting; edits inside keep anchors valid.
- [x] **FOLD-07** Fold commands — done 2026-10-03 (d5c439c)
  - Fold/unfold at cursor, fold all, unfold all, fold to level N.
- [x] **FOLD-08** Fold state kept as anchored ranges ([ADR 0006](docs/adr/0006-anchored-decorations.md)) — done 2026-10-03 (5cc4887)
- [x] **FOLD-09** Folding benchmarks and ADR — done 2026-10-03 (60d95d0)
  - Fold all / toggle / keystroke with many folds, scroll with folds on (with and without wrap) against the no-fold baseline; ADR 0014 records the design.
- [x] **FOLD-10** Demo integration — done 2026-10-03 (60d95d0)
  - `FoldColumn` in the demo gutter, a Fold menu (cursor, all, levels, cursor policy), tree-sitter ranges wired to the editor.

## M8 — Multi-cursor

**Exit criteria (demonstrated 2026-10-03: tests tst_input, tst_commands, tst_selectionset, tst_textsearch, tst_codeeditor; ADR 0015 benchmarks: 10,000 cursors type in 27 ms key-to-frame on 100 MB, one undo; hands-on mouse use in a real window not done):** add cursors above/below, add next occurrence, select all occurrences, and alt-drag box select all work; typing with 10,000 cursors on a large file stays interactive; one undo reverts a multi-cursor edit.

- [x] **MULTI-01** `SelectionSet` as a sorted, merged set — done 2026-10-03 (590f12f)
  - Overlapping or touching selections merge; primary cursor tracked.
- [x] **MULTI-02** Add cursor above/below and by ctrl/alt-click — done 2026-10-03 (65ef2b2)
- [x] **MULTI-03** Add next occurrence and select all occurrences — done 2026-10-03 (fb80bc4)
- [x] **MULTI-04** Alt-drag box (column) selection — done 2026-10-03 (fd8a8c1)
- [x] **MULTI-05** One transaction per multi-cursor edit — done 2026-10-03 (c0c8c27)
  - Edits applied back-to-front so earlier offsets stay valid; a single undo step.
- [x] **MULTI-06** Batched rendering of many cursors and selections — done 2026-10-03 (706ef90)
  - Overlays are found by binary search and bounded by the viewport ([ADR 0015](docs/adr/0015-multi-cursor.md)); a single-geometry batch proved unnecessary.
- [x] **MULTI-07** Per-cursor clipboard — done 2026-10-03 (3348431)
  - Copy joins per-cursor text by line; paste distributes when line counts match.

## M9 — Diagnostics & decorations

**Exit criteria (demonstrated 2026-10-05: tests tst_decorationset, tst_diagnostics, tst_decorations, tst_popup, tst_inlay, tst_wrapmap, tst_input; ADR 0016 benchmarks: 100k diagnostics (spread, dense, dense with wrap) scroll a 100 MB file at the 144 Hz display's rate with at most 1.8% dropped frames, one push takes about 130 ms; hands-on mouse use in a real window not done, scrolling with folds and diagnostics together and keystroke-to-frame with diagnostics not measured on the GPU because the display went away mid-session):** a host can push 100k diagnostics and the editor stays at 120 fps; squiggles, gutter icons and end-of-line messages render correctly with wrap and folds; hovering a diagnostic shows a QML popup.

- [x] **DIAG-01** Decoration API with anchored ranges — done 2026-10-05 (7cd058e)
  - Kinds: underline, squiggle, background, gutter icon, end-of-line virtual text. Ranges survive edits ([ADR 0006](docs/adr/0006-anchored-decorations.md), [ADR 0016](docs/adr/0016-decorations-and-diagnostics.md)).
  - Squiggle spans are built here and drawn by DIAG-02; inline virtual text is DIAG-06.
- [x] **DIAG-02** Squiggle rendering — done 2026-10-05 (09c06f8)
  - Custom `QSGMaterial`, or a tiled geometry fallback; correct across wrapped rows.
  - Done as the fallback: a pre-rendered wave strip per color in pooled image nodes, which every backend (software included) draws the same ([ADR 0016](docs/adr/0016-decorations-and-diagnostics.md)); the material is DIAG-07.
- [x] **DIAG-03** LSP-shaped diagnostic model — done 2026-10-05 (6697a5b)
  - Range, severity, message, code, source, related information, tags. LSP JSON in and out; shown through the decoration set ([ADR 0016](docs/adr/0016-decorations-and-diagnostics.md)).
- [x] **DIAG-04** Hover and popup placement — done 2026-10-05 (7f4b5a0)
  - Popups are QML delegate `Component`s positioned using `rectForPosition()`; flip to stay on screen.
  - `popupDelegate`, `hoverDelay`, `diagnosticPopups`; hover on text, gutter icon or end-of-line message; also opened by F8; the default popup's text is selectable and copyable ([ADR 0016](docs/adr/0016-decorations-and-diagnostics.md)). Checked on the software and Wayland GPU backends.
- [x] **DIAG-05** Go to next/previous diagnostic — done 2026-10-05 (df8c6c1)
  - `gotoNextDiagnostic`/`gotoPreviousDiagnostic` (wrapping, minimum severity, opens folds, scrolls); F8 / Shift+F8.
- [x] **DIAG-06** Inline virtual text and inlay hints — done 2026-10-05 (176cfa5)
  - Rendered as part of line layout so wrap and cursor movement account for them.
  - `setInlayHints` (LSP `InlayHint`) and `InlineText` decorations: injected into the row layout (a general injection list that the input-method composition now shares), widths fed to soft wrap, a pill behind each label ([ADR 0016](docs/adr/0016-decorations-and-diagnostics.md)).
- [x] **DIAG-08** Diagnostics benchmarks and demo — done 2026-10-05 (b0796a7)
  - `bench_decorations` (core: 100k diagnostics set, edits, queries, hints with wrap), `--diagnostics N` and `--dense` for `bench_scroll`, `--diagnostics N` for `bench_typing`; results in `benchmarks/results/2026-10-05-diagnostics-*.json`.
  - Demo: a Diagnostics menu (sample and 100k diagnostics, next/previous, messages, popups, icons, inlay hints), the icon column, and `--diagnostics`/`--hints` for screenshots.
  - Found by the benchmark and fixed: gutter images share one texture per image instead of uploading one per node ([ADR 0016](docs/adr/0016-decorations-and-diagnostics.md)).

## M10 — Vim mode

Solid core, not full Vim compatibility. Implemented as a second `InputHandler` over the command layer.

**Exit criteria (demonstrated 2026-10-07: tests tst_vim (about 380 table rows plus a key fuzzer, also clean under ASan/UBSan), tst_codeeditor (runtime switch, block cursor pixels, input-method gating, search highlight); ADR 0017 benchmarks: typical commands cost 2–80 µs on a 90 MB file, scrolling with the vim cursor or a match-everywhere highlight stays under the frame budget; hands-on use with a real input method and a real window not done):** a scripted vim test suite (keystrokes → expected buffer and cursor) passes; the vim handler can be switched on and off at runtime; dot-repeat and macros work across insert-mode edits.

- [x] **VIM-01** Vim `InputHandler` and mode state machine — done 2026-10-07 (8fd6542)
  - Normal, insert, replace, visual (char/line/block); mode-change signals for a host status bar.
  - `VimInputHandler` in `core/vim` (headless): modes Normal, Insert, Replace and the three visual modes, with OperatorPending and CommandLine as states; `mode`, `pendingKeys`, `commandLine`, `recordingRegister` and `message` feed a status bar; new `InputHandler` hooks (`activate`, `deactivate`, `cursorShape`, `cursorOffset`, `acceptsTextInput`, `commitText`); block and underline cursors; `CodeEditor.vimMode` and `vim` ([ADR 0017](docs/adr/0017-vim-input-handler.md)).
- [x] **VIM-02** Operator-pending grammar — done 2026-10-07 (34ef643)
  - `[count][register]operator[count]motion`, including doubled operators (`dd`, `yy`, `cc`).
  - Counts, registers and doubled operators for `d c y > < g~ gu gU`, and `x X s S D C Y p P J gJ r ~ <C-a> <C-x>`; vim's exclusive/inclusive/linewise rules, `cw` as `ce`, `dw` on the last word of a line.
- [x] **VIM-03** Motions — done 2026-10-07 (34ef643)
  - `hjkl`, `w b e W B E`, `0 ^ $`, `gg G`, `f t F T ; ,`, `% { } ( )`, `H M L`, `Ctrl-d/u/f/b`. Visual-row aware with wrap (`gj`/`gk`).
  - Also `| g_ gE g0 g^ g$ ]d [d zt zz zb zo zc zR zM`; paragraph and sentence scans are bounded (ADR 0017).
- [x] **VIM-04** Text objects — done 2026-10-07 (34ef643)
  - `iw aw`, `is as`, `ip ap`, quotes, brackets, tags.
  - Words, WORDs, sentences, paragraphs, quotes, brackets (counts widen; the inside of a multi-line block is its lines) and tags; in visual mode an object that is already selected grows.
- [x] **VIM-05** Registers — done 2026-10-07 (34ef643)
  - Unnamed, named `a–z` (append with `A–Z`), `0`, `1–9`, `+`/`*` mapped to the system clipboard.
  - Also `-`, `_` and the read-only `. : /`; one piece per cursor, distributed on paste.
- [x] **VIM-06** Dot-repeat — done 2026-10-07 (34ef643)
  - Replays the keys of the last change, insert-mode text and input-method commits included; a new count replaces the old one; visual changes repeat over a region of the same size; `"1p` steps through the numbered registers.
- [x] **VIM-07** Marks and jump list — done 2026-10-07 (34ef643)
  - Marks are anchors (`a-z A-Z ' . ^ < > [ ]`), the jump list holds 100 anchors, `<C-o>` and `<C-i>`.
- [x] **VIM-08** Macros (`q`, `@`) — done 2026-10-07 (34ef643)
  - A register holds the macro as notation text (`"ap` shows it, `@a` runs a yanked line); `@@`, counts, `:normal`; a macro or `.` is one undo step.
- [x] **VIM-09** Search — done 2026-10-07 (8fd6542)
  - `/ ? n N * #` with highlight of matches; shares the regex engine with find/replace (API-04).
  - The regex engine is `vim::compilePattern` (vim syntax to `QRegularExpression`) with `search::findRegex` and `findAllRegex` in `core/textsearch`, for API-04 to build on; plain-text patterns take a chunked fast path. `CodeEditor` marks matches in the frame plan (`searchMatch` theme color); `:noh`, `:set ic scs hls`.
- [x] **VIM-10** Visual block via multi-cursor — done 2026-10-07 (34ef643)
  - Block selection maps onto `SelectionSet`; `I`/`A`/`c` in block mode edit all rows.
  - `<C-v>` is one `SelectionSet` entry per line over a display-column range (tabs expanded, `$` to each line end); `I A c d y r ~ u U >` apply to every row in one undo step and leave the cursor at the block's top-left.
- [x] **VIM-11** Ex command subset — done 2026-10-07 (34ef643)
  - `:s`, `:g`, `:d`, `:N`, `:noh`; `:w`/`:q`/`:wq` emitted as signals to the host.
  - Ranges with marks, patterns and offsets; `:s` (`\1`, `\r`, `\U`, flags `g i I n`), `:g`, `:g!`, `:v`, `:d`, `:y`, `:normal`, `:>`, `:<`, `:N`, `:k`, `:noh`, `:set`; `:w :q :wq :x` and ZZ/ZQ as signals; unknown commands go out as `exCommand`.
- [x] **VIM-12** Multi-cursor interaction defined and tested — done 2026-10-07 (34ef643)
  - Every motion and operator applies per cursor; insert, `.`, macros and `p` work on the whole set; `<Esc>` in normal mode keeps the primary. Decided in ADR 0017, tested in `tst_vim` (`multi`).
- [x] **VIM-13** Vim test harness — done 2026-10-07 (34ef643)
  - Table-driven tests: initial text, keystrokes, expected text and selection.
  - `tests/core/tst_vim.cpp`: rows of initial text with markup (`|` cursor, `‹ ›` selection), keys in vim notation and the expected markup, run through `VimInputHandler` with a fake host; plus mode, signal, clipboard, highlight, key-event and fuzz tests.

## M11 — LSP-ready API & polish

**Exit criteria:** the demo app drives the editor from a mock language server exercising diagnostics, completion, hover and go-to-definition; the QML API is documented.

- [ ] **API-01** Document change stream in LSP incremental format
  - Plus position-encoding helpers (UTF-16 native, UTF-8/32 conversion on request).
- [ ] **API-02** Completion popup hooks
  - Host supplies items; editor owns placement, filtering UI and key handling.
- [ ] **API-03** Hover and signature-help popup hooks
- [ ] **API-04** Find/replace
  - Incremental search with regex, match highlighting, replace all as one transaction; runs on rope snapshots in a worker thread.
- [ ] **API-05** Go-to-definition and find-references request signals
- [x] **API-06** Matching bracket highlight (auto-pairing is INPUT-13) — done 2026-10-06 (8f0656d)
  - `matchBrackets` property (default on); the bracket next to each visible cursor and its partner get a background (`bracketMatch` theme color), with soft wrap and folds.
  - Raw-text match over the `autoClosePairs` that differ at both ends, scan bounded so a stray bracket in a huge file stays cheap; strings and comments are not recognised (API-10).
  - Benchmark: worst-case scan, typing and scroll with the highlight on and off.
- [ ] **API-07** Accessibility basics
  - Screen-reader text and cursor exposure through `QAccessible`.
- [ ] **API-08** Documented QML API
  - qdoc/markdown reference for all properties, signals and methods.
- [ ] **API-09** Code actions and quick-fix hooks
- [ ] **API-10** Bracket matching that skips strings and comments
  - Ask the tree-sitter tree for the bracket's partner (sibling bracket nodes under the same parent) instead of scanning, without moving the parse window; fall back to the raw scan outside the parsed range.
- [x] **API-11** Indent guides — done 2026-10-06 (67ce533)
  - `showIndentGuides` property (default on); a faint vertical line at every indent step (`indentWidth` columns, `tabWidth` when indenting with tabs) inside a line's leading whitespace, theme colors `indentGuide` and `indentGuideActive`. Blank lines continue the guides of the lines around them; with soft wrap only the hanging indent shows them; folded lines take theirs with them.
  - The guide of the block around the primary cursor is drawn in the active color: the pair of the bracket next to the cursor (the one API-06 highlights), else the innermost enclosing pair (bounded raw scan, cached until the next edit).
  - Guides of neighbouring rows are merged into one rectangle per column to keep the node count down.
  - Benchmark: enclosing-bracket scan and the per-frame indent lookup; scroll and typing with guides on and off.

- [x] **API-12** Language registration — done 2026-10-07 (e8cb87a)
  - `LanguageRegistry::registerLanguage(LanguageInfo)` (startup, GUI thread): hosts add or replace a language (built-ins can be overridden, not removed); queries as resource/file paths or inline strings; found by `find`/`detect`/`compiled` and as an injection target; lazy compile kept. A `SyntaxHighlighter` picks it up on its next `language`/`fileName` change ([ADR 0018](docs/adr/0018-host-extensions.md)).
  - Tests in `tst_languageregistry` (register, detect, highlight, replace, override, rejects, `compiled()` from several threads) and `tst_editorsyntax` (registered after the editor exists, Markdown fence injection).
- [x] **API-13** Host-defined token styles — done 2026-10-07 (e8cb87a)
  - `registerTokenStyle(name)` (192 slots), capture names map to them, `Theme.tokenStyles` entries (and `Theme.setTokenStyle`) take `color`, `background`, `bold`, `italic`; backgrounds are drawn behind the text by the editor.
- [x] **API-14** Highlight overlays — done 2026-10-07 (e8cb87a)
  - `CodeEditor.overlays`: any `Highlighter` painted over the main one, merged by the editor (`overlaySpans`), invalidating only the lines it names; several coexist, typed for qmlcachegen. Replaces wrapping a highlighter (Repose's `VariableHighlighter`).
  - Bracket match and search highlight stay per-frame rectangles (ADR 0018). Benchmarks: `bench_decorations` (merge), `bench_scroll --overlay`.

## M12 — Performance hardening

**Exit criteria:** all performance targets above are met and enforced by benchmark thresholds in CI; a measured decision on the glyph-atlas renderer is recorded in an ADR.

- [ ] **PERF-01** Very long lines
  - Shape and render only the visible horizontal window of lines longer than a threshold.
- [ ] **PERF-02** Multi-GB files
  - Lazy chunk loading; memory-mapped backing for untouched regions.
- [ ] **PERF-03** Memory profiling and trimming
  - Rope overhead, layout cache sizing, tree-sitter tree memory.
- [ ] **PERF-04** Evaluate a custom glyph-atlas monospace renderer vs. `QSGTextNode`
  - Decision recorded as an ADR; only adopt if benchmarks justify the complexity ([ADR 0001](docs/adr/0001-scene-graph-rendering.md)).
- [ ] **PERF-05** Benchmark regression thresholds
  - Fail CI when a tracked benchmark regresses beyond a set tolerance.
- [ ] **PERF-06** Render-thread audit
  - Confirm no main-thread work leaks into `updatePaintNode`; check batching and overdraw with `QSG_VISUALIZE`.
- [ ] **DIAG-07** Squiggle shader material (optional)
  - A `QSGMaterial` wave shader built with `qt_add_shaders` (Qt6::ShaderTools at build time only; the compiled `.qsb` is a resource, nothing new at run time), for waves that stay crisp at any zoom. The texture strip from DIAG-02 stays as the software-backend fallback.
  - Adopt only if a GPU-backend check shows a visible gain; needs a manual check on each backend (the software one cannot draw custom materials).
