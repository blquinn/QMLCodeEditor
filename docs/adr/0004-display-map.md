# 0004. Layered display map (fold → wrap → rows)

- Status: Accepted
- Date: 2026-10-02

## Context

Soft wrap is a core feature and folding follows later. Both change the relationship between buffer lines and on-screen rows: wrapping turns one line into many rows, folding hides many lines. If the renderer, hit-testing, scrolling and cursor movement each assume "row = buffer line", adding either feature later means touching all of them.

## Decision

Introduce a `DisplayMap` as the only authority on row layout, built from stacked transforms:

```
buffer lines → FoldMap → WrapMap → display rows
```

- Each layer is a summary tree giving O(log n) conversions in both directions (buffer position ↔ row, x).
- `FoldMap` starts as the **identity** transform. Wrap ships first (M4); the real folding layer (M8) replaces it without changing consumers.
- `WrapMap` stores a row count per buffer line in a summary tree. No-wrap mode is the same pipeline with `WrapMap` set to identity.
- Edits update the layers incrementally. On resize, the viewport lines are re-wrapped first and the remainder in the background; the scroll position is anchored to a buffer position so content doesn't jump.
- Rendering, hit-testing, scrolling, gutters and visual-row cursor movement all query the `DisplayMap`; none of them assume one line per row.

## Consequences

- Gutters can tell a first row from a continuation row, vim's `gj`/`gk` and default up/down movement work on display rows, and folding composes with wrapping.
- Total content height is only exact once wrapping has finished for the whole document; scroll bars use an estimate that is refined in the background.
- Slight indirection cost on every position query, accepted for the flexibility.
