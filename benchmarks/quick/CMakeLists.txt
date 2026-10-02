# Frame-timing hook for Quick benchmarks (and the demo): per-frame sync/render cost from a QQuickWindow.
qt_add_library(qce_bench_quick STATIC frametimer.h frametimer.cpp)
target_include_directories(qce_bench_quick PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qce_bench_quick PUBLIC qce_bench Qt6::Quick PRIVATE qce_warnings)

qce_add_test(tst_frametimer SOURCES tst_frametimer.cpp LIBS qce_bench_quick)
