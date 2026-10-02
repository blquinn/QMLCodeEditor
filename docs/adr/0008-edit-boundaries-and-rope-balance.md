# 0008. Edit boundaries and rope balance in practice

- Status: Accepted
- Date: 2026-10-02

## Context

Two details of [ADR 0007](0007-text-core-conventions.md) turned out to need refinement while implementing the text core.

1. A position (line, column) cannot name the offset between the `\r` and `\n` of a CRLF break, because columns exclude the `\r`. An edit that starts there would be reported to tree-sitter and LSP clients at a place that is not where the edit happened.
2. ADR 0007 promised at least 512 units in every leaf of an edited document. Joining two trees repairs the leaves at the seam, but a repair that merges two small leaves can leave the merged leaf small without touching its other neighbour.

## Decision

- `TextDocument` treats a CRLF break as one unit when it applies an edit: a start offset between the two moves back before the `\r`, an end offset between them moves forward past the `\n`. Surrogate pairs are treated the same way. The edit is also widened when its result would leave one of its ends inside a CRLF (for example inserting `x\r` before a `\n`, or deleting text between a `\r` and a `\n`), by pulling the neighbouring `\r` or `\n` into the removed and inserted text. Offsets in `TextChange` are therefore on both kinds of boundary in the old text and in the new text. `Rope` itself stays a plain unit sequence and edits it exactly as asked.
- `Rope::positionAt` snaps an offset inside CRLF back before the `\r`, so a column never lies beyond the line's content.
- The leaf minimum of 512 units is best effort. Hard invariants checked by `Rope::validate` are: uniform depth, fan-out of at most 16, non-empty leaves of at most 2048 units, and exact summaries. Seam repair (merge when the pair fits in a leaf, otherwise redistribute when either is below 512) keeps underfull leaves rare; `Rope::stats().underfullLeaves` makes that measurable, and tests assert it stays a small fraction.

## Consequences

- Backspace at the start of a CRLF line removes the whole break, which is the behaviour editors want.
- A host that really needs to put text between a `\r` and `\n` has to go through `Rope` directly.
- If benchmarks show fragmentation hurting, a rebalancing pass can be added without changing the API.
