# QML-facing items and scene-graph rendering. Depends on core.
qt_add_library(QMLCodeEditor STATIC)
qt_add_qml_module(QMLCodeEditor
    URI me.blq.qmlcodeeditor
    VERSION 1.0
    SOURCES codeeditor.cpp codeeditor.h theme.cpp theme.h textmetrics.cpp textmetrics.h linelayoutcache.cpp linelayoutcache.h editorscene.cpp editorscene.h
    gutter.cpp gutter.h linenumbercolumn.cpp linenumbercolumn.h markercolumn.cpp markercolumn.h
    changecolumn.cpp changecolumn.h delegatecolumn.cpp delegatecolumn.h foldcolumn.cpp foldcolumn.h
    decorationcolumn.cpp decorationcolumn.h popupplacement.cpp popupplacement.h
    QML_FILES DiagnosticPopup.qml
)

target_compile_definitions(QMLCodeEditor
    PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
target_link_libraries(QMLCodeEditor PUBLIC QMLCodeEditor::Core PRIVATE Qt6::Quick Qt6::Concurrent qce_warnings)
target_include_directories(QMLCodeEditor PUBLIC ${PROJECT_SOURCE_DIR}/src)
