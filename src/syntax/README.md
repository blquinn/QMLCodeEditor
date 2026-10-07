# src/syntax

tree-sitter integration and the language registry (milestone M6, [ADR 0013](../../docs/adr/0013-tree-sitter-highlighting.md)). Depends on `core`, implements the
`Highlighter` interface that `quick` consumes, and is also the QML module `me.blq.qmlcodeeditor.syntax` (`SyntaxHighlighter`).

- `languageregistry`: languages, detection, compiled queries
- `ropeinput`, `parsejob`: parsing rope snapshots on a worker, injections
- `queryinfo`: capture to token style, text predicates
- `treesitterhighlighter`: edits, scheduling, span cache
- `TreeSitterFoldProvider` (in `treesitterhighlighter`): fold ranges from `queries/<language>/folds.scm` captures (`@fold`, `@fold.keep_last`), indentation where the tree does not reach ([ADR 0014](../../docs/adr/0014-code-folding.md))
- Hosts add languages with `LanguageRegistry::registerLanguage` at startup (grammar function, extensions, aliases, queries as files or inline text, [ADR 0018](../../docs/adr/0018-host-extensions.md))
