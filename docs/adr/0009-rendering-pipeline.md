# 0009. Rendering pipeline: polish builds a frame plan, sync reconciles pooled nodes

- Status: Accepted
- Date: 2026-10-02

## Context

[ADR 0001](0001-scene-graph-rendering.md) fixed the approach (a `QQuickItem` with `updatePaintNode`, pooled `QSGTextNode`s, scrolling by transform) but left open how work is divided between threads, how a document millions of pixels tall fits in float matrices, and how overlays are drawn on every scene-graph backend. M2 had to settle these to hit 120 fps on a 100 MB file.

## Decision

- **Polish builds, sync reconciles.** `updatePolish()` (GUI thread) decides which rows are on screen plus a margin of half a viewport each side, lays those lines out into `QTextLayout`s (through an LRU `LineLayoutCache` keyed by buffer line) and stores a *frame plan*: the rows and shared pointers to their layouts, plus the cursor, selection and current-line spans. `updatePaintNode()` (render thread, GUI blocked) only walks the plan and edits nodes. It does no layout and allocates nothing in steady state. `QSGTextNode::addTextLayout` copies glyphs, and layouts are `shared_ptr`s, so the cache may evict a layout the plan still holds.
- **Scrolling is one matrix.** All rows hang under a scroll `QSGTransformNode`; each row has its own transform and text node. If the layout window still covers the viewport, polish does nothing and a scroll step rewrites the scroll matrix only. Rows that leave the plan donate their node pair to rows that enter (pool of up to 128 spares), so scrolling allocates nothing.
- **Row positions are relative to an origin row.** Matrices are `float`; 2M rows × 20 px would lose sub-pixel precision. Row transforms are relative to an origin row near the viewport, the scroll matrix carries the large offset (computed in `double`), and the origin is re-based when the viewport is more than 2048 rows from it.
- **Display rows come from `DisplayMap`.** A core `DisplayMap` (identity for now: row == line) is the only thing the editor asks for row ↔ line and position ↔ row, so wrap and folds (WRAP-01, FOLD-01) change its internals, not its consumers ([ADR 0004](0004-display-map.md)).
- **Overlays are `QSGRectangleNode`s.** Current line, selection, whitespace marks and the cursor are rectangle nodes in four fixed groups (z-order: current line, selection, marks, text, cursor). The renderer batches them because they share a material, and rectangle nodes are drawn natively on every backend. A custom `QSGGeometryNode` with indexed triangles was tried first and silently draws nothing on the software backend, which the tests rely on. Blink toggles the cursor group's opacity; it never touches geometry. Squiggles (M9) still need a custom material and will be tested on the GPU backends.
- **Monospace first, ligatures off.** `TextMetrics` gives a cell grid for lines of printable ASCII and tabs in a monospace font; other lines are measured through their layout. Layouts use a font with kerning and `liga`/`clig`/`calt` disabled so shaped advances equal the grid. A host that wants ligatures gives up the fast path (a later option).
- **Visible whitespace is drawn by us.** `QSGTextNode` does not render `QTextOption::ShowTabsAndSpaces`. Spaces are laid out as middle dots (same column in a monospace font) and tabs get a thin rectangle mark; `LineLayout::text` keeps the original line so positions are computed from the real text.
- **Highlighting emits style IDs.** `Highlighter` (core) returns `{start, length, TokenStyle}` per line; `Theme` (quick) turns a style into a `QTextCharFormat`. `syntax` therefore never sees colors.
- **Long lines are laid out whole.** A line is one `QTextLayout` however long. Horizontal windowing of very long lines is PERF-01 / WRAP-09; the M2 benchmark covers many-short-lines files only.

## Consequences

- Frame cost is bounded by the viewport, not the file: on a 100 MB file a smooth scroll step costs about 0.3 ms of polish, 0.15 ms of sync and 0.3 ms of render (`benchmarks/results/2026-10-02-scroll.json`). Random jumps, where every layout is cold, cost about 2 ms of polish.
- Memory beyond the rope is independent of file size: the layout cache is capped (about 300 lines) and scrolling grows resident memory by under 1 MB.
- Polish does layout on the GUI thread. A very long line or a huge jump can therefore stall input; moving layout of cold lines to a worker is an option if PERF work shows the need.
- The frame plan couples the item to the scene through `FrameParams`; adding decorations (M9) means adding spans to it, not new paths through the item.
