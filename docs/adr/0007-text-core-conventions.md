# 0007. Text core conventions: line breaks, offsets and code points

- Status: Accepted
- Date: 2026-10-02

## Context

[ADR 0002](0002-rope-text-storage.md) fixes the storage (a persistent UTF-16 rope) but leaves several conventions open that every later layer (rendering, tree-sitter, LSP, vim) depends on: what ends a line, how `\r\n` counts, what a column is, and who is responsible for keeping surrogate pairs intact.

## Decision

- **Raw storage.** The rope holds the text exactly as loaded. Mixed or CRLF line endings survive a load/save round trip byte for byte; nothing is normalized in memory.
- **Line breaks.** `\n` ends a line. A `\r` directly before `\n` belongs to the break: it is excluded from the line's length and from columns. A lone `\r` is not a line break. This diverges from LSP, which also treats a lone `\r` as a break; files that depend on it are rare and a pass over the text on every edit to support them is not worth it.
- **Offsets and columns** are UTF-16 code units (the rope's native unit and the LSP default). Line numbers and columns are zero-based.
- **Rope structure makes no surrogate assumptions.** A leaf boundary may fall between the halves of a surrogate pair, as ADR 0002 already allows. The rope is a plain sequence of UTF-16 units and `Rope::at`/`slice`/edits work on units.
- **Whole code points are guaranteed at the edges of the rope, not inside it:**
  - `ChunkIterator` never yields a chunk that ends in the middle of a pair; when a pair straddles two leaves it yields the pair as its own two-unit chunk. This is what the tree-sitter input callback and search consume.
  - The document layer (`TextDocument`) and position helpers snap offsets that point into the middle of a pair to the pair's start before editing, and never report such an offset.
- **Balance invariants** (checked by `Rope::validate`): all leaves at the same depth; at most 16 children per branch; leaves hold at most 2048 units and, in a document built through edits, at least 512 units (a lone root leaf, and the edge leaves of a `slice`, are exempt). Branches may become underfull after deletions; depth stays logarithmic.
- **Loading is exclusive.** While a progressive file load is in flight the document is read-only; edits are refused until it finishes or is cancelled. This keeps the loader's appends and user edits from needing a merge.

## Consequences

- Edits are cheap and simple: no seam repair for surrogates, no line-ending normalization pass.
- Anything that needs code points or grapheme clusters (movement, vim) goes through `TextBoundaries` (CORE-04), never raw offset arithmetic.
- Hosts that need LSP's lone-`\r` behaviour have to convert at the API boundary (API-01).
