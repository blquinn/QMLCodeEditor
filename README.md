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

The sanitizer presets are compiler-agnostic. If your GCC lacks the sanitizer runtimes (Fedora needs `libasan`,
`libubsan`, `libtsan`), configure them with clang: `CXX=clang++ cmake --preset asan-ubsan`.

## Using the module

```qml
import me.blq.qmlcodeeditor

CodeEditor { anchors.fill: parent }
```

## Roadmap tooling

```sh
python3 tools/roadmap.py          # progress
python3 tools/roadmap.py --next   # next item per milestone
python3 tools/roadmap.py --check  # validate ROADMAP.md
```
