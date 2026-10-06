# 0016. Decorations and diagnostics: one anchored set, spans in the frame plan

- Status: Proposed
- Date: 2026-10-05

## Context

M9 adds squiggles, underlines, backgrounds, gutter icons, end-of-line messages and inlay hints, a diagnostic model that hosts can feed straight from a language server, hover popups and next/previous navigation. [ADR 0006](0006-anchored-decorations.md) fixed the principles (anchored ranges, scene-graph rendering of the hot parts, QML for popups) and [ADR 0012](0012-gutter.md) noted that a decoration system with many layers should share one structure instead of one per column. The exit bar is a host pushing 100k diagnostics without the editor dropping below 120 fps.

## Decision

### Decorations (DIAG-01)

- **One `DecorationSet` per editor** (core), shared by everything that decorates: the diagnostic model, hosts calling `addDecoration`, gutter icons. A decoration is a pair of anchors in the document's `AnchorSet` plus a look (kind, color, text, severity, priority, icon). By default the start leans right and the end left, so typing at either edge does not grow it; hosts can override both.
- **Layers.** Every decoration belongs to a layer, and `setLayer` replaces one layer's contents in a single call (sort, create anchors, merge into the sorted vector). Layer 0 is the host's default; negative layers belong to the editor (diagnostics).
- **Storage follows `LineMarkerSet`:** a vector sorted by start offset (anchors keep their order under edits), binary search, and a bound on how far back an entry can reach (`maxSpan`, grown by what edits insert and recomputed after enough drift). Two additions. A decoration longer than 64k units goes to a short unsorted list that every query checks, because a single whole-file background would make the bound useless for all the others. And starts that lean left can end up out of order with right-leaning ones when an edit replaces the text between them, so when any exist the slice an edit touched (the entries whose start is in `[start, newEnd]`, which is all that can have moved relative to its neighbours) is sorted again.
- **Queries are bounded by the plan.** `buildDecorations()` asks for the offsets of the rows in the plan (the same range selections use, which takes in text folded under the last row) and turns the answer into per-row spans, as selections do. A decoration on lines hidden by a fold draws nothing; one that runs from hidden text to visible text starts at the first visible line after the fold; one that ends in hidden text ends at its header. Output is capped per frame.
- **Spans reach the scene as `ColoredSpan`s** (the gutter's rectangle type). Backgrounds go in the backdrop between the current-line band and the selection; underlines go in a group above the text and below the cursor. Both reuse `ColorBatch`.
- **End-of-line text is part of the row's layout** but not of its text. `LineLayout::width` still ends where the text does (selections, hit-testing and the cursor stop there); `fullWidth` includes the message and feeds the content width and the fold chip's position (the chip follows the message). The message is italic in the severity color. Columns computed from a click in the message are clamped to the row. With soft wrap on, a message that does not fit the row is cut with an ellipsis, since a wrapped row has nowhere to put it; layouts are rebuilt whenever the wrap width changes, so the cut follows resizing.
- **Gutter icons** come from `DecorationColumn`, a normal gutter column: the highest-priority `GutterIcon` decoration starting on each visible line, on its first row. With no icon of its own a decoration gets a glyph for its severity.
- **Signals.** Only calls that add or remove decorations emit `changed(firstLine, lastLine, kinds)`; edits move decorations silently, because the editor already repaints on every edit. The editor drops the cached layouts of the affected lines when virtual text changed and rebuilds the plan otherwise.

### Squiggles (DIAG-02)

- **A texture strip, not a material.** A custom `QSGMaterial` was the first idea and the ROADMAP's, but a geometry node with a custom material draws nothing on the software backend, which the test suite runs on ([ADR 0009](0009-rendering-pipeline.md) hit the same wall with overlays), and it would add `Qt6::ShaderTools` as a build dependency (build time only: `qsb` compiles the shader and the `.qsb` is a resource). Squiggles are therefore `QSGImageNode`s, which every backend draws the same way. The shader remains an option (DIAG-07) for crisper waves at any zoom; the strip stays as the software fallback.
- **One strip per color.** A zigzag a pixel wide, four logical pixels per wave and three high, drawn with `QPainter` at the window's device pixel ratio into a 512-pixel strip (a whole number of waves) and uploaded once per color. Image nodes share the texture instead of owning it, so the renderer batches them; textures of colors that no node uses any more are freed once more than 16 exist.
- **Phase comes from the source rectangle.** A span becomes one node (more only when it is wider than the strip) whose rectangle starts at the span's pixel-rounded x and whose source rectangle starts `x mod period` pixels into the strip. Waves of neighbouring spans, wrapped rows and re-laid-out rows therefore line up. Nearest filtering keeps the pre-rendered wave from blurring. Verified on the software backend in the tests and by eye on the Wayland GPU backend (the two renderings are identical).
- **Wrapped rows** get a piece per row, because spans are built per row; an empty range gets one cell of wave.

## Consequences

- Decorations do not change the cost of a frame without them: one counter check per layout and per plan build.
- A text reset clears every layer, like markers do; a host that wants decorations back pushes them again.

(Sections for squiggles, diagnostics, popups and inlay hints follow with their items.)
