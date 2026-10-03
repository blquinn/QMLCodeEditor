# src/syntax

tree-sitter integration and the language registry (milestone M6, [ADR 0013](../../docs/adr/0013-tree-sitter-highlighting.md)). Depends on `core`, implements the
`Highlighter` interface that `quick` consumes, and is also the QML module `me.blq.qmlcodeeditor.syntax` (`SyntaxHighlighter`).

- `languageregistry`: languages, detection, compiled queries
- `ropeinput`, `parsejob`: parsing rope snapshots on a worker, injections
- `queryinfo`: capture to token style, text predicates
- `treesitterhighlighter`: edits, scheduling, span cache
