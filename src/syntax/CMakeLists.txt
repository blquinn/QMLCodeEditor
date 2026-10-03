# tree-sitter integration and language registry (M6). Depends on core only (ADR 0003).
# The QML module (SyntaxHighlighter) needs only Qt Qml, never Quick.
qt_add_library(qce_syntax STATIC)
qt_add_qml_module(qce_syntax
    URI me.blq.qmlcodeeditor.syntax
    VERSION 1.0
    SOURCES
        languageregistry.h languageregistry.cpp
        queryinfo.h queryinfo.cpp
        ropeinput.h ropeinput.cpp
        parsejob.h parsejob.cpp
        treesitterhighlighter.h treesitterhighlighter.cpp
)
add_library(QMLCodeEditor::Syntax ALIAS qce_syntax)
target_include_directories(qce_syntax PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qce_syntax PUBLIC QMLCodeEditor::Core qce_grammars Qt6::Qml PRIVATE qce_warnings)

# Query files become resources at :/qce/syntax/<language>/<kind>.scm. A file in src/syntax/queries/<language>/
# replaces the upstream one of the same name (for queries that don't compile against the pinned grammar).
set(QCE_QUERY_KINDS highlights injections)
set(_query_files)
foreach(pair IN LISTS QCE_TS_SOURCE_DIRS)
    string(REPLACE "=" ";" parts "${pair}")
    list(GET parts 0 lang)
    list(GET parts 1 dir)
    foreach(kind IN LISTS QCE_QUERY_KINDS)
        set(local ${CMAKE_CURRENT_SOURCE_DIR}/queries/${lang}/${kind}.scm)
        set(upstream ${dir}/queries/${kind}.scm)
        if(NOT EXISTS ${upstream}) # some repositories keep queries/<language>/<kind>.scm
            set(upstream ${dir}/queries/${lang}/${kind}.scm)
        endif()
        if(EXISTS ${local})
            set(file ${local})
        elseif(EXISTS ${upstream})
            set(file ${upstream})
        else()
            continue()
        endif()
        set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${lang}/${kind}.scm)
        list(APPEND _query_files ${file})
    endforeach()
endforeach()
qt_add_resources(qce_syntax qce_syntax_queries PREFIX /qce/syntax BASE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_query_files})
