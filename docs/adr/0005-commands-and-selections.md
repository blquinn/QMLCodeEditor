# 0005. Commands over an always-present selection set

- Status: Accepted
- Date: 2026-10-02

## Context

Multi-cursor and vim mode both stress an editor's input model. If the base editor handles one cursor and key events edit the document directly, multi-cursor becomes a bolt-on loop over special cases, and vim becomes a second, parallel editing path that bypasses undo, wrap-aware movement and decorations.

## Decision

- The editor state always holds a `SelectionSet`. A single cursor is a set of size one; there is no separate single-cursor code path.
- Every mutation is a **command** (insert text, delete range, move, indent, ...) applied to the selection set. Commands are the only thing that touches the buffer, and a multi-cursor edit is one transaction (one undo step), applied back-to-front.
- Key handling is a swappable `InputHandler` that translates key events into commands. The default handler and the vim handler are two implementations of the same interface, and vim can be toggled at runtime.
- Movement commands ask the `DisplayMap` ([ADR 0004](0004-display-map.md)) for row-aware positions, so wrap and folds are respected by both handlers.

## Consequences

- Multi-cursor (M6) mostly means enlarging the set and testing; vim visual-block maps onto it directly.
- Vim and the default handler share undo, clipboard and movement code instead of duplicating it.
- INPUT-01 and INPUT-02 carry real design weight and must land before the rest of M3.
