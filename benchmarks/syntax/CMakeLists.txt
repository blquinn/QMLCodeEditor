# Syntax highlighting benchmarks (SYNTAX-10): parse time, window parse, edit latency, memory per MB.
# Run from a release build: build/release/benchmarks/syntax/bench_syntax
qt_add_executable(bench_syntax bench_syntax.cpp)
target_link_libraries(bench_syntax PRIVATE qce_bench QMLCodeEditor::Syntax qce_warnings)
target_compile_definitions(bench_syntax PRIVATE QCE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME bench_syntax_smoke
    COMMAND bench_syntax --quick --json ${CMAKE_CURRENT_BINARY_DIR}/bench_syntax_smoke.json)
set_tests_properties(bench_syntax_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
