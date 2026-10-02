# Text core: Qt Core only, must build and test without a window system (ADR 0003).
qt_add_library(qce_core STATIC
    qcecore.h qcecore.cpp
    rope.h rope.cpp
)
add_library(QMLCodeEditor::Core ALIAS qce_core)

target_compile_definitions(qce_core PUBLIC QCE_VERSION_STRING="${PROJECT_VERSION}")
target_include_directories(qce_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qce_core PUBLIC Qt6::Core PRIVATE qce_warnings)
