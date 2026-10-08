cmake_minimum_required(VERSION 3.21)

project(QMLCodeEditor VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Minimum Qt is 6.8 LTS (public QSGTextNode API, ADR 0001); development and CI use 6.11.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Test Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

# Link-time optimization for Release builds (ThinLTO with clang). Must come before any target is created.
option(QCE_LTO "Enable link-time optimization in Release builds" ON)
if(QCE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT QCE_IPO_SUPPORTED OUTPUT QCE_IPO_MESSAGE LANGUAGES C CXX)
    if(QCE_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(WARNING "QCE_LTO requested but not supported: ${QCE_IPO_MESSAGE}")
    endif()
endif()

enable_testing()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(QceWarnings)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME roadmap_check
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/roadmap.py --check)
    add_test(NAME benchtrack_selftest
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/test_benchtrack.py)
endif()

add_subdirectory(src/core)
add_subdirectory(src/quick)
option(QCE_SYNTAX "Build tree-sitter syntax highlighting (downloads tree-sitter and grammars with CPM)" ON)
if(QCE_SYNTAX)
    include(QceTreeSitter)
    add_subdirectory(src/syntax)
endif()
add_subdirectory(tests)
if(QCE_SYNTAX) # the demo highlights with tree-sitter
    add_subdirectory(demo)
endif()

option(QCE_BUILD_BENCHMARKS "Build the benchmark suite" ON)
if(QCE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
