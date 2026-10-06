# Text core benchmarks. Run from a release build: build/release/benchmarks/core/bench_core
qt_add_executable(bench_core bench_core.cpp)
target_link_libraries(bench_core PRIVATE qce_bench QMLCodeEditor::Core qce_warnings)

qt_add_executable(bench_fold bench_fold.cpp)
target_link_libraries(bench_fold PRIVATE qce_bench QMLCodeEditor::Core qce_warnings)

qt_add_executable(bench_decorations bench_decorations.cpp)
target_link_libraries(bench_decorations PRIVATE qce_bench QMLCodeEditor::Core qce_warnings)
