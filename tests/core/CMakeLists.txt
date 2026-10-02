qce_add_test(tst_core_smoke SOURCES tst_core_smoke.cpp LIBS QMLCodeEditor::Core)
qce_add_test(tst_rope SOURCES tst_rope.cpp testutil.h LIBS QMLCodeEditor::Core)
qce_add_test(tst_positions SOURCES tst_positions.cpp testutil.h LIBS QMLCodeEditor::Core)
qce_add_test(tst_snapshot SOURCES tst_snapshot.cpp testutil.h LIBS QMLCodeEditor::Core Qt6::Concurrent)
