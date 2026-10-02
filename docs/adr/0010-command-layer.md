# 0010. Command layer, selection set and input handlers

- Status: Accepted
- Date: 2026-10-02

## Context

[ADR 0005](0005-commands-and-selections.md) decided that every mutation is a command on an always-present selection set and that key handling is swappable. M3 has to turn that into code: where the pieces live, how selections follow edits, how multi-selection edits reach undo, and how movement gets pixel geometry that `core` cannot have.

## Decision

- **Everything but rendering and the clipboard lives in `core`.** `SelectionSet`, the commands, `InputHandler` and the default handler depend on Qt Core/Gui only (key events are Gui types). `CodeEditor` supplies geometry and the system clipboard.
- **`SelectionSet` stores anchors.** Each selection is two `AnchorSet` anchors (right gravity), so selections follow every edit, including ones made through the document API rather than a command. `set()` clamps offsets, moves them off code point interiors, sorts, and merges overlapping or touching selections; the primary selection follows its merge. A document reset puts a single cursor at 0, and so does every slice of a progressive load (the cursor must not ride along with appended text).
- **Commands are free functions over an `EditContext`** (document, selection set, settings). They all funnel into `commands::applyReplacements`: a sorted list of `[start, end) -> text` replacements, applied back to front so earlier offsets stay valid. One replacement uses an ordinary document edit tagged with an `EditKind` (typing and backspace runs coalesce in `UndoStack`); several use an edit group, so a multi-selection edit is a single undo step. Selections before and after travel with the undo step and are restored by undo/redo. A selection with nothing to do contributes a no-op replacement so its cursor stays in the set.
- **Movement asks a `CursorLayout`** for x positions and row hit-testing, since goal columns and page size are pixel concepts. The editor implements it from its text metrics and the `DisplayMap`; tests use a monospace grid implementation. Rows always come from the display map ([ADR 0004](0004-display-map.md)).
- **Input handlers translate events into commands.** An `InputHandler` receives key events and text input with the `EditContext` and an `InputHost` for the things only the item can do (clipboard, scrolling). `DefaultInputHandler` uses `QKeySequence::StandardKey` matching so platform shortcuts are right; a vim handler (M10) implements the same interface.
- **Selection change signalling is batched.** `SelectionSet::Batch` holds back `changed()` across a multi-step edit and emits once.

## Consequences

- Multi-cursor (M6) enlarges the set; commands already iterate it. M6 still has to make the per-frame overlay build and anchor churn scale to 10,000 selections (today `set()` recreates every anchor, and `buildOverlays()` walks all selections).
- A command that is not a document edit (movement, select all) never touches undo history, but breaks typing coalescing so the next typed run is its own step.
- The cursor does not stay on the end of a replaced text: `setText`, `load` and `reset` all land it at 0.
