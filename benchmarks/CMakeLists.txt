add_subdirectory(harness)
add_subdirectory(quick)

qt_add_executable(qce_datagen datagen/main.cpp)
target_link_libraries(qce_datagen PRIVATE qce_bench qce_warnings)

qt_add_executable(bench_noop bench_noop.cpp)
target_link_libraries(bench_noop PRIVATE qce_bench qce_warnings)
add_test(NAME bench_noop
    COMMAND bench_noop --iterations 3 --json ${CMAKE_CURRENT_BINARY_DIR}/bench_noop.json)
