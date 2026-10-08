add_subdirectory(harness)
add_subdirectory(quick)
if(QCE_SYNTAX)
    add_subdirectory(syntax)
endif()

qt_add_executable(qce_datagen datagen/main.cpp)
target_link_libraries(qce_datagen PRIVATE qce_bench qce_warnings)

qt_add_executable(bench_noop bench_noop.cpp)
target_link_libraries(bench_noop PRIVATE qce_bench qce_warnings)
add_test(NAME bench_noop
    COMMAND bench_noop --iterations 3 --json ${CMAKE_CURRENT_BINARY_DIR}/bench_noop.json)
add_subdirectory(core)

# Record a run of the tracked suites (benchmarks/tracked.json) into benchmarks/history and compare it with the
# previous one: cmake --build --preset release --target bench_record. Informational; not part of `all` or ctest.
if(Python3_FOUND)
    set(_qce_tracked bench_core bench_find bench_fold bench_decorations bench_brackets bench_vim
        bench_scroll bench_typing bench_wrap)
    if(QCE_SYNTAX)
        list(APPEND _qce_tracked bench_syntax)
    endif()
    add_custom_target(bench_record
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/benchtrack.py record
                --build-dir ${CMAKE_BINARY_DIR} $<$<BOOL:${QCE_BENCH_NOTE}>:--note> $<$<BOOL:${QCE_BENCH_NOTE}>:${QCE_BENCH_NOTE}>
        DEPENDS ${_qce_tracked}
        USES_TERMINAL
        COMMENT "Recording tracked benchmarks")
endif()
