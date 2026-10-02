qt_add_executable(qce_demo main.cpp)
qt_add_qml_module(qce_demo
    URI Demo
    VERSION 1.0
    QML_FILES Main.qml
)
target_link_libraries(qce_demo PRIVATE Qt6::Quick QMLCodeEditorplugin qce_warnings)

# Launches the window, waits for the first presented frame, exits 0 (non-zero after 10 s).
add_test(NAME demo_smoke COMMAND qce_demo --smoke)
set_tests_properties(demo_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
