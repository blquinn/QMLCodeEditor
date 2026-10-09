# Text core: Qt Core only, must build and test without a window system (ADR 0003).
qt_add_library(qce_core STATIC
    qcecore.h qcecore.cpp
    rope.h rope.cpp
    textposition.h textsnapshot.h textchange.h
    textdocument.h textdocument.cpp
    anchorset.h anchorset.cpp
    fileformat.h fileloader.h fileloader.cpp
    filesaver.h filesaver.cpp
    textboundaries.h textboundaries.cpp indentation.h indentation.cpp
    selection.h selectionset.h selectionset.cpp commands.h commands.cpp
    cursorlayout.h cursorlayout.cpp inputhandler.h inputhandler.cpp
    undostack.h undostack.cpp
    textsearch.h textsearch.cpp findreplace.h findreplace.cpp bracketmatch.h bracketmatch.cpp indentguides.h indentguides.cpp
    highlighter.h highlighter.cpp
    displaymap.h displaymap.cpp
    linemarkerset.h linemarkerset.cpp decorationset.h decorationset.cpp diagnostics.h diagnostics.cpp inlayhints.h inlayhints.cpp
    vim/vimkeys.h vim/vimkeys.cpp vim/vimmotions.h vim/vimmotions.cpp vim/vimregex.h vim/vimregex.cpp
    vim/vimtextobjects.h vim/vimtextobjects.cpp vim/vimhandler.h vim/vimhandler.cpp vim/vimoperators.cpp vim/vimex.cpp
    longlineindex.h longlineindex.cpp foldmap.h foldmap.cpp foldprovider.h foldprovider.cpp folding.h folding.cpp wrapmeasure.h wrapmeasure.cpp wrapbreaks.h wrapbreaks.cpp wrapmap.h wrapmap.cpp
)
add_library(QMLCodeEditor::Core ALIAS qce_core)

target_compile_definitions(qce_core PUBLIC QCE_VERSION_STRING="${PROJECT_VERSION}")
target_include_directories(qce_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(qce_core PUBLIC Qt6::Core Qt6::Gui PRIVATE qce_warnings)
