qce_add_test(tst_languageregistry SOURCES tst_languageregistry.cpp LIBS QMLCodeEditor::Syntax Qt6::Concurrent)
qce_add_test(tst_ropeinput SOURCES tst_ropeinput.cpp LIBS QMLCodeEditor::Syntax)
qce_add_test(tst_treesitterhighlighter SOURCES tst_treesitterhighlighter.cpp LIBS QMLCodeEditor::Syntax)
qce_add_test(tst_editorsyntax SOURCES tst_editorsyntax.cpp
    LIBS Qt6::Quick QMLCodeEditor QMLCodeEditorplugin QMLCodeEditor::Syntax qce_syntaxplugin)
qce_add_test(tst_foldquery SOURCES tst_foldquery.cpp LIBS QMLCodeEditor::Syntax)
