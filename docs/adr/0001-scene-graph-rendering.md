# 0001. Render with the scene graph, not `QQuickPaintedItem`

- Status: Accepted
- Date: 2026-10-02

## Context

The scaffold derives from `QQuickPaintedItem`, which rasterizes through `QPainter` into an FBO or image and then uploads it as a texture. That is simple, but it is CPU-bound for text, re-rasterizes large areas on scroll, and gives up the scene graph's batching. An editor that must scroll large documents at high frame rates cannot afford it.

`TextEdit` and `Text` are not an option either: they lay out the whole document, expose no control over virtualization, and make multi-cursor, wrap-aware gutters and custom decorations awkward.

Qt 6.7 added public text node API (`QQuickWindow::createTextNode()`, `QSGTextNode::addTextLayout()`), which renders `QTextLayout` through the scene graph's distance-field/native glyph machinery without private headers.

## Decision

- `CodeEditor` is a plain `QQuickItem` with `ItemHasContents` and an `updatePaintNode()` implementation.
- Text is rendered with pooled `QSGTextNode`s, one per visible line (or small group of lines), fed by per-line `QTextLayout`. Only viewport lines are laid out; a layout cache is invalidated per line on edit.
- Scrolling moves a parent transform node; it does not rebuild geometry.
- Cursors, selections, current-line and other backgrounds are batched into a small number of geometry nodes. Squiggles use a custom `QSGMaterial`.
- Fonts are monospace-first: a cell-grid fast path for ASCII/monospace runs, with `QTextLayout` measurement as the fallback so CJK, emoji, ligatures and tabs remain correct.
- Minimum Qt is 6.8 LTS (public `QSGTextNode`); development and CI use 6.11.
- A custom glyph-atlas renderer is **deferred**. PERF-04 evaluates it against benchmarks and records the outcome in a new ADR.

## Consequences

- Rendering state lives on the render thread; the sync step (`updatePaintNode`) must stay cheap. Layout work happens on the GUI thread beforehand.
- We own scrolling, hit-testing and text metrics, which is more code but is what lets wrap, folds and gutters work together.
- Behavior depends on the public `QSGTextNode` API staying fast enough; the escape hatch is the atlas renderer.
