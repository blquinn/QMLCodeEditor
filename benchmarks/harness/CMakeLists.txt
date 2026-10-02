# Benchmark runner and synthetic data generator. Qt Core only.
qt_add_library(qce_bench STATIC bench.h bench.cpp datagen.h datagen.cpp)
target_include_directories(qce_bench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qce_bench PUBLIC Qt6::Core PRIVATE qce_warnings)

qce_add_test(tst_bench_harness SOURCES tst_bench_harness.cpp LIBS qce_bench)
