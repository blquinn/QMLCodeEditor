# 0013. Tree-sitter highlighting: worker parses, edited trees, window parses

- Status: Accepted
- Date: 2026-10-03

## Context

Highlighting must never block typing, must work on a 100 MB file, and must not flicker while a reparse is in flight. The editor asks a `Highlighter` for one line per display row on the GUI thread (RENDER-09).

## Decision

**Modules.** `src/syntax` (`qce_syntax`, QML module `me.blq.qmlcodeeditor.syntax`) depends on core and Qt Qml only. `TreeSitterHighlighter` is the QML element `SyntaxHighlighter`. tree-sitter and the grammars are fetched with CPM at pinned tags and SHA-256 (`cmake/QceTreeSitter.cmake`); `QCE_SYNTAX=OFF` skips all of it (and the demo). `Highlighter` gained `attach(TextDocument*)`/`detach()`; `CodeEditor::highlighter` is a QML property.

**Parsing.** One worker job at a time on the global `QThreadPool`. `RopeInput` feeds the rope snapshot as UTF-16LE (bytes = 2 x units, `TSPoint` columns count bytes), so no whole-document `QString` is built. Results come back through a mailbox drained on the GUI thread; the highlighter can be destroyed while a parse runs (it only flips a cancel flag, and the parse callback checks it).

**Edits.** Every `TextChange` becomes a `TSInputEdit` applied at once to the GUI thread's trees, and is logged. A job parses a snapshot at version V from a copy of the (edited) tree. When it lands, the log entries after V are replayed on the new tree, so a result for an older version is carried forward, not discarded; only a different generation (text reset, language change) is dropped. This is also the interim highlighting (SYNTAX-09): spans are queried from the edited old tree until the new one lands, so nothing flickers. It replaces the roadmap's "shifted through anchors".

**Large files.** Documents up to `windowSize` (2 M units) are parsed whole. Larger ones are parsed over a window (included ranges, so tree coordinates stay document coordinates) of about 1000 lines / 2 M units around the last line asked for; below `fullParseLimit` (16 M units, ~32 MB) a whole-document parse follows in the background, above it only windows are used and asking for lines outside the window parses a new one. A cancelled whole-document parse yields to a window parse when the viewport has no highlights. A window starting inside a construct (block comment, string) can misparse until a full tree exists; accepted.

**Spans.** `highlightLines` is served from an LRU of 64-line blocks. A miss runs the highlight query once over the block (byte range) per overlapping tree, sorts captures, and sweeps them with a stack: nested captures override outer ones; for an identical range the more specific capture name wins (`string.special.key` over `string`), then the later pattern (upstream queries follow both conventions). Unknown capture names are ignored, `@none` resets. Text predicates (`#eq?`, `#match?`, `#any-of?` and negations) are evaluated against the rope; others are ignored. Pattern fragments that do not compile against the pinned grammar are dropped one by one instead of failing the language.

**Invalidation (SYNTAX-07).** On landing, the lines reported by `ts_tree_get_changed_ranges`, the lines edited since the previous result, and, when the covered region moved, old and new region are dropped from the cache and announced with `invalidated(first, last)`.

**Injections (SYNTAX-08).** After a parse the worker runs the language's `injections.scm` over a region around the viewport (the whole text up to 300 k units) and parses each content range, or each `injection.combined` group, with its language (name from `#set!` or a capture, resolved through registry aliases), nested up to 3 deep. Layer trees are painted over the host. QML needs none: the qmljs grammar parses JavaScript itself. Plain `.js` uses the JavaScript grammar.

## Consequences

- A keystroke costs the GUI thread an edit plus a few block fills (0.15-0.5 ms measured on 1 M units); parsing never runs there.
- Incremental reparse time depends on tree shape and the grammar's scanner, not on this code: a flat module of 10 k classes reparses in 13 ms (UTF-8 or UTF-16) against 79 ms full, a Python sample with docstrings and comprehensions in 200 ms. Real files are far smaller.
- Memory is 27 MB (C++) to 156 MB (Markdown, injections included) per million units of source while a whole tree is held; windows keep a 100 MB file at ~420 MB RSS in total.
- Languages are added in `LanguageRegistry` plus a grammar in `QceTreeSitter.cmake`; a query that fails upstream can be overridden in `src/syntax/queries/<lang>/`.
- Not done: folds and locals queries (M7), TypeScript as its own language, incremental reparse of injected trees (they are rebuilt per result).
