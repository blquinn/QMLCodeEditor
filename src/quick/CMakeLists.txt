# QML-facing items and scene-graph rendering. Depends on core.
qt_add_library(QMLCodeEditor STATIC)
qt_add_qml_module(QMLCodeEditor
    URI me.blq.qmlcodeeditor
    VERSION 1.0
    SOURCES codeeditor.cpp codeeditor.h theme.cpp theme.h textmetrics.cpp textmetrics.h
)

target_compile_definitions(QMLCodeEditor
    PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
target_link_libraries(QMLCodeEditor PUBLIC QMLCodeEditor::Core PRIVATE Qt6::Quick qce_warnings)
target_include_directories(QMLCodeEditor PUBLIC ${PROJECT_SOURCE_DIR}/src)
