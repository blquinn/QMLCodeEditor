# Architecture Decision Records

Short records of decisions that are expensive to reverse. Roadmap items link to the ADR that motivates them.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-scene-graph-rendering.md) | Render with the scene graph, not `QQuickPaintedItem` | Accepted |
| [0002](0002-rope-text-storage.md) | Persistent UTF-16 rope for text storage | Accepted |
| [0003](0003-module-layout.md) | Core / Quick / Syntax module split | Accepted |
| [0004](0004-display-map.md) | Layered display map (fold → wrap → rows) | Accepted |
| [0005](0005-commands-and-selections.md) | Commands over an always-present selection set | Accepted |
| [0006](0006-anchored-decorations.md) | Anchored ranges for decorations; QML for popups | Accepted |
| [0007](0007-text-core-conventions.md) | Text core conventions: line breaks, offsets and code points | Accepted |
| [0008](0008-edit-boundaries-and-rope-balance.md) | Edit boundaries and rope balance in practice | Accepted |
| [0009](0009-rendering-pipeline.md) | Rendering pipeline: polish builds a frame plan, sync reconciles pooled nodes | Accepted |
| [0010](0010-command-layer.md) | Command layer, selection set and input handlers | Accepted |
| [0011](0011-soft-wrap.md) | Soft wrap: estimated row counts, refined on demand and in the background | Accepted |
| [0012](0012-gutter.md) | Gutter inside the editor: columns paint into the frame plan | Accepted |
| [0013](0013-tree-sitter-highlighting.md) | Tree-sitter highlighting: worker parses, edited trees, window parses | Accepted |
| [0014](0014-code-folding.md) | Code folding: anchored folds, hidden lines in the wrap layer | Accepted |
| [0015](0015-multi-cursor.md) | Multi-cursor: reused anchors, grouped edits that coalesce, culled overlays | Accepted |
| [0016](0016-decorations-and-diagnostics.md) | Decorations and diagnostics: one anchored set, spans in the frame plan | Proposed |
| [0017](0017-vim-input-handler.md) | Vim as an input handler over the command layer | Accepted |
| [0018](0018-host-extensions.md) | Host extensions: registered languages, token styles and highlight overlays | Accepted |
| [0019](0019-find-and-replace.md) | Find and replace: one search layer, worker threads on snapshots, an opt-in find bar | Accepted |

## Adding one

Copy this template to `NNNN-short-title.md` (next number, never reused) and add a row above. To change a decision, write a new ADR and mark the old one `Superseded by NNNN`.

```markdown
# NNNN. Title

- Status: Proposed | Accepted | Superseded by NNNN
- Date: YYYY-MM-DD

## Context
What forces are at play?

## Decision
What we will do.

## Consequences
What gets easier, what gets harder, what we'll revisit.
```
