# Frame-timing hook for Quick benchmarks (and the demo): per-frame sync/render cost from a QQuickWindow.
qt_add_library(qce_bench_quick STATIC frametimer.h frametimer.cpp)
target_include_directories(qce_bench_quick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qce_bench_quick PUBLIC qce_bench Qt6::Quick PRIVATE qce_warnings)

qce_add_test(tst_frametimer SOURCES tst_frametimer.cpp LIBS qce_bench_quick)

# Scroll and frame-time benchmark over generated large files (RENDER-11). Run from a release build on a real display.
qt_add_executable(bench_scroll bench_scroll.cpp)
target_link_libraries(bench_scroll PRIVATE qce_bench_quick QMLCodeEditor qce_warnings)
add_test(NAME bench_scroll_smoke COMMAND bench_scroll --quick --json ${CMAKE_CURRENT_BINARY_DIR}/bench_scroll_smoke.json)
set_tests_properties(bench_scroll_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

# Keystroke-to-frame benchmark (INPUT-11). Run from a release build on a real display.
qt_add_executable(bench_typing bench_typing.cpp)
target_link_libraries(bench_typing PRIVATE qce_bench_quick QMLCodeEditor qce_warnings)
add_test(NAME bench_typing_smoke COMMAND bench_typing --quick --json ${CMAKE_CURRENT_BINARY_DIR}/bench_typing_smoke.json)
set_tests_properties(bench_typing_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

# Soft-wrap benchmark (WRAP-10). Run from a release build on a real display.
qt_add_executable(bench_wrap bench_wrap.cpp)
target_link_libraries(bench_wrap PRIVATE qce_bench_quick QMLCodeEditor qce_warnings)
add_test(NAME bench_wrap_smoke COMMAND bench_wrap --quick --json ${CMAKE_CURRENT_BINARY_DIR}/bench_wrap_smoke.json)
set_tests_properties(bench_wrap_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
