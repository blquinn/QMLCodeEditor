# 0006. Anchored ranges for decorations; QML for popups

- Status: Accepted
- Date: 2026-10-02

## Context

Diagnostics, fold regions, marks, search matches and highlights all refer to ranges of text that move as the user edits. Storing absolute offsets means rewriting every decoration on every keystroke. Meanwhile the editor needs rich popups (hovers, completion lists, diagnostics messages) that hosts will want to style and extend.

## Decision

- Decorations and marks are ranges whose endpoints are **anchors** into the rope (CORE-06), with configurable left/right gravity, so they track edits without being rewritten.
- Decorations are rendered inside the scene graph (underlines, squiggles via a custom material, backgrounds, gutter icons, end-of-line virtual text), batched per frame. Only decorations intersecting the visible rows are touched.
- The decoration model is LSP-shaped (range, severity, message, code, source, related information) so hosts can forward diagnostics directly.
- Popups and hovers are **QML delegate `Component`s** supplied by the host (with defaults), placed using `rectForPosition()` on the editor. They are rare and interactive, so QML cost is acceptable and styling stays in the host's hands.

## Consequences

- Hot paths (many decorations, many frames) stay in C++/scene graph; cold paths (a popup appearing) stay in QML.
- The anchor structure must scale to 100k+ live anchors; this is a requirement on CORE-06.
- Folds ([FOLD-08](../../ROADMAP.md)) reuse the same mechanism, so folded regions survive edits without special handling.
