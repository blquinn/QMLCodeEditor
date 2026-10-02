# QMLCodeEditor

A high-performance QML code editor widget (Qt 6, minimum 6.8, developed on 6.11). It renders through the Qt Quick scene graph instead of `TextEdit` or `QQuickPaintedItem`, and targets very large documents. Tree-sitter will provide syntax highlighting.

## Where things are written down

- `ROADMAP.md`: milestones M0–M12 and every tracked work item. This is the source of truth for what to build and in what order.
- `docs/adr/`: architecture decisions. Read the relevant ADR before changing the area it covers; write a new ADR (don't edit an accepted one) when reversing a decision.
- `tools/roadmap.py`: progress report (`python3 tools/roadmap.py`), next items (`--next`), validation (`--check`).

## Working with the roadmap

- Pick work from `ROADMAP.md`. If the work isn't there, add an item first (next free ID in its area; IDs are never renumbered or reused).
- Item line format: `- [ ] **AREA-NN** Title`, with acceptance criteria as indented bullets. Markers: `[ ]` todo, `[~]` in progress, `[x]` done, `[-]` dropped.
- Set `[~]` when you start an item. When finished, set `[x]` and append `— done YYYY-MM-DD (shorthash)`. A dropped item says `dropped: <reason>` in its line.
- Reference the ID in commit messages (`CORE-03: add rope snapshots`).
- Run `python3 tools/roadmap.py --check` after editing `ROADMAP.md`.
- A milestone is complete only when its exit criteria are demonstrated, not merely when its boxes are ticked.

## Architecture rules (see ADRs)

- Render with `QQuickItem::updatePaintNode` and pooled `QSGTextNode`s; never lay out the whole document. (0001)
- Text lives in the UTF-16 rope; don't convert to `QString` for whole-document operations. (0002)
- `src/core` depends only on Qt Core/Gui and must be testable without a window. (0003)
- Nothing may assume one buffer line equals one display row; go through `DisplayMap`. (0004)
- All edits are commands on a `SelectionSet`; vim and the default keymap are `InputHandler`s. (0005)
- Decorations are anchored ranges; popups are QML delegates. (0006)

## Performance

Performance is the product. Changes touching rendering, the rope, the display map or highlighting need a benchmark result (or a note on why none applies). Targets are in `ROADMAP.md`.
