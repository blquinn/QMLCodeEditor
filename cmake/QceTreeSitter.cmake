# tree-sitter core and grammars, fetched with CPM at pinned tags (SYNTAX-01).
# Sources are downloaded (not configured): each grammar's generated parser.c / scanner.c is compiled
# here into one static library per grammar. Set CPM_SOURCE_CACHE to share downloads between builds.

include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

function(qce_fetch name url hash)
    CPMAddPackage(NAME ${name} URL ${url} URL_HASH SHA256=${hash} DOWNLOAD_ONLY YES)
    set(${name}_SOURCE_DIR ${${name}_SOURCE_DIR} PARENT_SCOPE)
endfunction()

qce_fetch(tree_sitter
    https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.27.0.tar.gz
    d35c96e68736bd9569d2757c3cc71052485f33082c3825f1aed9d0e86013a159)
qce_fetch(ts_json
    https://github.com/tree-sitter/tree-sitter-json/archive/refs/tags/v0.24.8.tar.gz
    acf6e8362457e819ed8b613f2ad9a0e1b621a77556c296f3abea58f7880a9213)
qce_fetch(ts_javascript
    https://github.com/tree-sitter/tree-sitter-javascript/archive/refs/tags/v0.25.0.tar.gz
    9712fc283d3dc01d996d20b6392143445d05867a7aad76fdd723824468428b86)
qce_fetch(ts_markdown
    https://github.com/tree-sitter-grammars/tree-sitter-markdown/archive/refs/tags/v0.5.3.tar.gz
    df845b1ab7c7c163ec57d7fa17170c92b04be199bddab02523636efec5224ab6)

qce_fetch(ts_html
    https://github.com/tree-sitter/tree-sitter-html/archive/refs/tags/v0.23.2.tar.gz
    21fa4f2d4dcb890ef12d09f4979a0007814f67f1c7294a9b17b0108a09e45ef7)
# Also contains the DTD grammar (unused); the XML grammar is in its xml/ directory.
qce_fetch(ts_xml
    https://github.com/tree-sitter-grammars/tree-sitter-xml/archive/refs/tags/v0.7.0.tar.gz
    4330a6b3685c2f66d108e1df0448eb40c468518c3a66f2c1607a924c262a3eb9)

add_library(qce_tree_sitter STATIC ${tree_sitter_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(qce_tree_sitter
    PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include
    PRIVATE ${tree_sitter_SOURCE_DIR}/lib/src ${tree_sitter_SOURCE_DIR}/lib/src/wasm)
target_compile_definitions(qce_tree_sitter PRIVATE _POSIX_C_SOURCE=200112L _DEFAULT_SOURCE _BSD_SOURCE _DARWIN_C_SOURCE)
set_target_properties(qce_tree_sitter PROPERTIES C_STANDARD 11 C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_compile_options(qce_tree_sitter PRIVATE -w)

# qce_add_grammar(<target> DIR <dir containing src/>)  compiles parser.c and scanner.c if present.
function(qce_add_grammar target dir)
    set(sources ${dir}/src/parser.c)
    if(EXISTS ${dir}/src/scanner.c)
        list(APPEND sources ${dir}/src/scanner.c)
    endif()
    add_library(${target} STATIC ${sources})
    target_include_directories(${target} PRIVATE ${dir}/src)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
    target_compile_options(${target} PRIVATE -w)
endfunction()

qce_add_grammar(qce_ts_json ${ts_json_SOURCE_DIR})
qce_add_grammar(qce_ts_javascript ${ts_javascript_SOURCE_DIR})
qce_add_grammar(qce_ts_html ${ts_html_SOURCE_DIR})
qce_add_grammar(qce_ts_xml ${ts_xml_SOURCE_DIR}/xml)
qce_add_grammar(qce_ts_markdown ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown)
qce_add_grammar(qce_ts_markdown_inline ${ts_markdown_SOURCE_DIR}/tree-sitter-markdown-inline)

add_library(qce_grammars INTERFACE)
target_link_libraries(qce_grammars INTERFACE
    qce_tree_sitter qce_ts_json qce_ts_javascript
    qce_ts_html qce_ts_xml qce_ts_markdown qce_ts_markdown_inline)

# language=dir pairs of the upstream checkouts, read by src/syntax to embed query files.
set(QCE_TS_SOURCE_DIRS
    json=${ts_json_SOURCE_DIR} javascript=${ts_javascript_SOURCE_DIR}
    html=${ts_html_SOURCE_DIR} xml=${ts_xml_SOURCE_DIR}
    markdown=${ts_markdown_SOURCE_DIR}/tree-sitter-markdown
    markdown_inline=${ts_markdown_SOURCE_DIR}/tree-sitter-markdown-inline
)
