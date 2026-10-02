cmake_minimum_required(VERSION 3.16)

project(QMLCodeEditor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Minimum Qt is 6.8 LTS (public QSGTextNode API, ADR 0001); development and CI use 6.11.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_library(QMLCodeEditor STATIC)
qt_add_qml_module(QMLCodeEditor
    URI me.blq.qmlcodeeditor
    VERSION 1.0
    SOURCES codeeditor.cpp codeeditor.h
)

target_compile_definitions(QMLCodeEditor
    PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
target_link_libraries(QMLCodeEditor
    PRIVATE Qt6::Quick)

target_include_directories(QMLCodeEditor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
