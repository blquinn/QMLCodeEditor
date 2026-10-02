# 0002. Persistent UTF-16 rope for text storage

- Status: Accepted
- Date: 2026-10-02

## Context

The buffer must handle files of 100 MB to multiple GB, frequent edits at many positions at once (multi-cursor), and concurrent readers (tree-sitter parsing, search) that must not block typing. A flat `QString` makes edits O(n); a gap buffer handles one cursor well but not many; a piece table makes line lookups awkward and degrades as it fragments.

Three external systems share one string encoding: `QString` is UTF-16, tree-sitter can parse UTF-16 directly (`TSInputEncodingUTF16LE`), and LSP positions default to UTF-16 code units.

## Decision

Store text in a persistent (immutable-node, reference-counted) B-tree rope whose leaves are UTF-16 chunks of roughly 1–4 KB.

- Each node summarizes its UTF-16 length and newline count, so offset ↔ (line, column) is O(log n).
- Edits copy only the path from root to leaf, so a snapshot is O(1): it is just a root pointer. Background threads read snapshots without locking.
- Insert, delete and replace are O(log n), including many edits in a single multi-cursor transaction (applied back-to-front).
- Grapheme/word boundary helpers sit on top of the rope rather than inside it, so the tree stays encoding-simple.

## Consequences

- No encoding conversion at the tree-sitter or LSP boundaries; conversion happens only on file load/save and for non-UTF-16 LSP servers.
- Chunk boundaries can fall between surrogate halves internally. The public API must never expose a split pair, and the tree-sitter input callback must hand over contiguous slices.
- More implementation work than `QString`, but it is the foundation for fast load, cheap undo history (snapshots) and thread-safe readers.
