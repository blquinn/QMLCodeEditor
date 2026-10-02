cmake_minimum_required(VERSION 3.21)

project(QMLCodeEditor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Minimum Qt is 6.8 LTS (public QSGTextNode API, ADR 0001); development and CI use 6.11.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Test Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(QceWarnings)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME roadmap_check
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/roadmap.py --check)
endif()

add_subdirectory(src/core)
add_subdirectory(src/quick)
add_subdirectory(tests)
add_subdirectory(demo)

option(QCE_BUILD_BENCHMARKS "Build the benchmark suite" ON)
if(QCE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
