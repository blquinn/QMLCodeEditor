# QMLCodeEditor

A high-performance code editor item for QML (Qt 6). It renders through the Qt Quick scene graph rather than
`TextEdit` or `QQuickPaintedItem`, and targets very large documents. See [`ROADMAP.md`](ROADMAP.md) for the
plan and [`docs/adr/`](docs/adr/README.md) for architecture decisions.

## Requirements

- Qt **6.8 or newer** (6.8 LTS is the floor because the public `QSGTextNode` API arrived in 6.7). Development
  and CI use **Qt 6.11**.
- CMake 3.21+, a C++20 compiler, Ninja (the presets use it).

## Build, test, benchmark

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `release`, `asan-ubsan`, `tsan`. Tests run with `QT_QPA_PLATFORM=offscreen`.

Benchmarks are recorded and compared over time with `cmake --build --preset release --target bench_record`; see
[`benchmarks/history/README.md`](benchmarks/history/README.md).

The presets build with clang (`clang++` must be on `PATH`), which also provides the sanitizer runtimes. To use
another compiler, override it at configure time: `cmake --preset debug -DCMAKE_CXX_COMPILER=g++`.

The `tsan` preset uses `tools/tsan.supp` because the system Qt is not TSan-instrumented; see that file for what
it hides and why.

## Using the module

```qml
import me.blq.qmlcodeeditor

CodeEditor { anchors.fill: parent }
```

### Text rendering

`renderType` picks how glyphs are rasterized, with the same values as `Text.renderType`
(`CodeEditor.QtRendering`, `CodeEditor.NativeRendering`, `CodeEditor.CurveRendering`). Unset, the editor follows
the application default set with `QQuickWindow::setTextRenderType()` (read on every frame, so changing it later
is picked up), and reading the property returns that effective value. Assign a value to override it and
`undefined` to go back to following the default. Layout and metrics don't depend on it. `renderTypeQuality` is
not exposed: `QSGTextNode` has no equivalent.

```qml
CodeEditor { renderType: CodeEditor.NativeRendering }
```

### Extending the editor

A host adds languages, token styles and highlights without touching the library (ADR 0018). All three are
C++ calls made once at startup, before editors parse.

```cpp
extern "C" const TSLanguage *tree_sitter_graphql();   // from the host's own parser.c

qce::registerTokenStyle(u"graphql.directive");        // styles first: a query's captures are fixed at compile time
qce::LanguageInfo graphql;
graphql.id = u"graphql"_s;
graphql.name = u"GraphQL"_s;
graphql.extensions = {u"graphql"_s, u"gql"_s};
graphql.aliases = {u"gql"_s};
graphql.grammar = &tree_sitter_graphql;
graphql.highlightQueries = {u":/myapp/graphql/highlights.scm"_s};  // a resource or file path...
graphql.highlightSource = u"(directive) @graphql.directive"_s;    // ...and/or the query text itself
QString error;
if (!qce::LanguageRegistry::registerLanguage(graphql, &error))
  qWarning() << error;
```

A `SyntaxHighlighter` with `fileName: "q.graphql"` or `language: "gql"` now highlights it, Markdown fences
tagged `gql` too. Registering again with the same id replaces a language (built-ins included).

A style gets its look from the theme, by name, with an optional background drawn behind the text:

```qml
Theme {
    Component.onCompleted: setTokenStyle("graphql.directive", "#c586c0", "transparent", true /*bold*/, false)
}
```

An overlay is any `Highlighter` listed in `CodeEditor.overlays`. Its spans are painted over the main
highlighter's, so a host can mark `{{variables}}` or search hits without wrapping anything. It says which
lines changed through `invalidated()`, and only those are laid out again.

```qml
CodeEditor {
    highlighter: SyntaxHighlighter { language: "json" }
    overlays: [ VariableHighlighter { request: root.request } ]   // the host's own Highlighter subclass
}
```

```cpp
class VariableHighlighter : public qce::Highlighter {
  qce::TokenStyle defined = qce::registerTokenStyle(u"variable.defined");
  QList<QList<qce::HighlightSpan>> highlightLines(const qce::TextSnapshot &text, qsizetype first, qsizetype last) override;
  // spans sorted by start, not overlapping; emit invalidated(first, last) when the answer changes
};
```

## Roadmap tooling

```sh
python3 tools/roadmap.py          # progress
python3 tools/roadmap.py --next   # next item per milestone
python3 tools/roadmap.py --check  # validate ROADMAP.md
```

`ctest` runs `--check` as the `roadmap_check` test. To also validate on every commit that touches
`ROADMAP.md`, enable the bundled hook once per clone:

```sh
git config core.hooksPath tools/hooks
```
