# 0003. Core / Quick / Syntax module split

- Status: Accepted
- Date: 2026-10-02

## Context

Most of the editor's logic (rope, selections, undo, display map, vim state machine) has nothing to do with QML. If it is entangled with `QQuickItem`, it can only be tested through a running scene, which is slow and flaky. The tree-sitter dependency is large and should not be forced on users of the core.

## Decision

```
src/core/    Qt Core/Gui only: rope, anchors, undo, selections, commands,
             display map, input handlers (default + vim)
src/quick/   QML-facing items and scene-graph rendering
src/syntax/  tree-sitter integration and language registry
demo/        standalone demo app
tests/       Qt Test suites (core tests run without a window system)
benchmarks/  repeatable performance benchmarks
third_party/ tree-sitter and grammars (CMake FetchContent, pinned)
```

`core` has no dependency on `quick` or `syntax`. `quick` depends on `core`. `syntax` depends on `core` and implements the `Highlighter` interface that `quick` consumes.

## Consequences

- Core logic is unit-testable and benchmarkable headlessly (offscreen platform, or no GUI at all).
- Interfaces between layers (`Highlighter`, `InputHandler`, gutter providers) have to be designed deliberately, which is the point.
- The existing flat files (`codeeditor.cpp/.h`, `CodeEditorControls.qml`) move into this layout in INFRA-02 and INFRA-04.
