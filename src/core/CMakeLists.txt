# Text core: Qt Core only, must build and test without a window system (ADR 0003).
qt_add_library(qce_core STATIC
    qcecore.h qcecore.cpp
    rope.h rope.cpp
    textposition.h textsnapshot.h textchange.h
    textdocument.h textdocument.cpp
    anchorset.h anchorset.cpp
    fileformat.h fileloader.h fileloader.cpp
    filesaver.h filesaver.cpp
    textboundaries.h textboundaries.cpp
    selection.h undostack.h undostack.cpp
    highlighter.h highlighter.cpp
)
add_library(QMLCodeEditor::Core ALIAS qce_core)

target_compile_definitions(qce_core PUBLIC QCE_VERSION_STRING="${PROJECT_VERSION}")
target_include_directories(qce_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qce_core PUBLIC Qt6::Core PRIVATE qce_warnings)
