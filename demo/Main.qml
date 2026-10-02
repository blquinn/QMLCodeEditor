import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import me.blq.qmlcodeeditor

ApplicationWindow {
    id: window

    // Set from the command line by main.cpp.
    property url initialFile
    // The file shown, for Save; empty for an unnamed buffer.
    property url currentFile
    // Chrome colors follow the editor theme; the Basic style (set in main.cpp) draws from this palette
    // instead of the system's.
    property bool darkMode: true
    // Lines carrying a bookmark star, by line number.
    property var bookmarks: ({})

    palette {
        window: darkMode ? "#1e1f22" : "#f2f2f2"
        windowText: darkMode ? "#d4d4d4" : "#202020"
        base: darkMode ? "#2b2d30" : "#ffffff"
        text: darkMode ? "#d4d4d4" : "#202020"
        button: darkMode ? "#3a3d41" : "#e4e4e4"
        buttonText: darkMode ? "#e0e0e0" : "#202020"
        highlight: darkMode ? "#3d6fb4" : "#4a89dc"
        highlightedText: "#ffffff"
        mid: darkMode ? "#55595e" : "#b0b0b0"
        dark: darkMode ? "#55595e" : "#909090"
        light: darkMode ? "#4a4d52" : "#ffffff"
        placeholderText: darkMode ? "#8a8d91" : "#808080"
    }

    // A tool button that lights up under the pointer (the Basic style only reacts to presses).
    // Buttons with nothing to do are dimmed rather than disabled: a disabled item gets no hover events,
    // and the highlight is still useful feedback.
    component DemoToolButton: ToolButton {
        id: button
        property bool available: true
        opacity: available ? 1 : 0.45
        background: Rectangle {
            implicitWidth: 40
            implicitHeight: 40
            color: button.down ? window.palette.highlight
                 : button.hovered ? (window.darkMode ? "#4d5258" : "#cfd3d8")
                                  : "transparent"
        }
    }

    // A scroll bar with a clearly visible thumb on a track slightly apart from the editor background.
    component DemoScrollBar: ScrollBar {
        id: bar
        implicitWidth: 14
        implicitHeight: 14
        policy: ScrollBar.AlwaysOn
        background: Rectangle {
            color: window.darkMode ? "#26282b" : "#e6e6e6"
        }
        contentItem: Rectangle {
            implicitWidth: 14
            implicitHeight: 14
            radius: 3
            anchors.margins: 2
            color: bar.pressed ? (window.darkMode ? "#a8abb0" : "#606060")
                 : bar.hovered ? (window.darkMode ? "#8c8f94" : "#808080")
                               : (window.darkMode ? "#6e7277" : "#9a9a9a")
        }
    }

    width: 1100
    height: 720
    visible: true
    title: qsTr("QMLCodeEditor demo")

    Component.onCompleted: {
        if (initialFile.toString() !== "") {
            currentFile = initialFile
            editor.load(initialFile)
        }
        editor.forceActiveFocus()
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 12

            DemoToolButton {
                text: qsTr("Open…")
                onClicked: openDialog.open()
            }
            DemoToolButton {
                text: qsTr("Save")
                available: window.currentFile.toString() !== ""
                onClicked: if (available) editor.save(window.currentFile)
            }
            DemoToolButton {
                text: qsTr("Undo")
                available: editor.canUndo
                onClicked: editor.undo()
            }
            DemoToolButton {
                text: qsTr("Redo")
                available: editor.canRedo
                onClicked: editor.redo()
            }
            DemoToolButton {
                text: qsTr("Cut")
                onClicked: editor.cut()
            }
            DemoToolButton {
                text: qsTr("Copy")
                onClicked: editor.copy()
            }
            DemoToolButton {
                text: qsTr("Paste")
                onClicked: editor.paste()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("%1 lines").arg(editor.lineCount.toLocaleString())
            }
        }
    }

    footer: ColumnLayout {
        spacing: 0

        // Editor options.
        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 12

                CheckBox {
                    text: qsTr("Read-only")
                    onToggled: editor.readOnly = checked
                }
                CheckBox {
                    text: qsTr("Tabs")
                    onToggled: editor.insertSpaces = !checked
                }
                CheckBox {
                    text: qsTr("Dark")
                    checked: window.darkMode
                    onToggled: {
                        window.darkMode = checked
                        editor.theme.applyPreset(checked ? "dark" : "light")
                    }
                }
                CheckBox {
                    text: qsTr("Whitespace")
                    onToggled: editor.showWhitespace = checked
                }
                Label { text: qsTr("Tab") }
                SpinBox {
                    from: 1; to: 16; value: editor.tabWidth
                    onValueModified: editor.tabWidth = value
                }
                Label { text: qsTr("Font") }
                SpinBox {
                    from: 6; to: 48; value: editor.font.pointSize
                    onValueModified: {
                        var f = editor.font
                        f.pointSize = value
                        editor.font = f
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }

        // Soft wrap.
        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 12

                Label { text: qsTr("Wrap") }
                ComboBox {
                    model: [qsTr("Off"), qsTr("Window"), qsTr("Column")]
                    currentIndex: editor.wrapMode
                    onActivated: (index) => editor.wrapMode = index
                }
                SpinBox {
                    visible: editor.wrapMode === CodeEditor.WrapAtColumn
                    from: 10; to: 400; value: editor.wrapColumn
                    onValueModified: editor.wrapColumn = value
                }
                CheckBox {
                    text: qsTr("Words")
                    checked: editor.wordWrap
                    onToggled: editor.wordWrap = checked
                }
                CheckBox {
                    text: qsTr("Hanging indent")
                    checked: editor.wrapIndent
                    onToggled: editor.wrapIndent = checked
                }
                Label { text: qsTr("Extra") }
                SpinBox {
                    from: 0; to: 16; value: editor.wrapIndentExtra
                    onValueModified: editor.wrapIndentExtra = value
                }
                Label { text: qsTr("Numbers") }
                ComboBox {
                    model: [qsTr("Absolute"), qsTr("Relative"), qsTr("Hybrid")]
                    currentIndex: lineNumbers.mode
                    onActivated: (index) => lineNumbers.mode = index
                }
                CheckBox {
                    text: qsTr("Line numbers")
                    checked: lineNumbers.visible
                    onToggled: lineNumbers.visible = checked
                }
                CheckBox {
                    text: qsTr("Changes")
                    checked: changeBars.visible
                    onToggled: changeBars.visible = checked
                }
                CheckBox {
                    text: qsTr("Breakpoints")
                    checked: breakpoints.visible
                    onToggled: breakpoints.visible = checked
                }
                CheckBox {
                    text: qsTr("Bookmarks")
                    checked: bookmarkColumn.visible
                    onToggled: bookmarkColumn.visible = checked
                }
                Item { Layout.fillWidth: true }
            }
        }

        // Status.
        ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                Label {
                    text: editor.loading ? qsTr("Loading… %1%").arg(Math.round(editor.loadProgress * 100))
                                         : editor.wrapping ? qsTr("Wrapping…") : statusText
                    property string statusText: qsTr("Ready")
                    id: statusLabel
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("Ln %1, Col %2").arg(editor.cursorLine + 1).arg(editor.cursorColumn + 1)
                          + (editor.selectionEnd > editor.selectionStart
                             ? qsTr("  (%1 selected)").arg(editor.selectionEnd - editor.selectionStart) : "")
                }
                Label { text: qsTr("%1 fps").arg(fps.value.toFixed(0)) }
            }
        }
    }

    // Frame rate, measured only while the editor is scrolling. Updating a label schedules a frame, and a
    // frame would update the label again, so reacting to every frame would keep the demo rendering forever.
    // Instead frames are counted and the label is refreshed twice a second, and only if scrolling happened.
    QtObject {
        id: fps
        property real value: 0
        property int frames: 0
        property bool scrolled: false
    }
    Connections {
        target: window
        function onFrameSwapped() { fps.frames++ }
    }
    Connections {
        target: editor
        function onContentYChanged() { fps.scrolled = true }
        function onContentXChanged() { fps.scrolled = true }
    }
    Timer {
        interval: 500
        repeat: true
        running: true
        onTriggered: {
            if (fps.scrolled)
                fps.value = fps.frames * 1000 / interval
            fps.scrolled = false
            fps.frames = 0
        }
    }

    CodeEditor {
        id: editor
        objectName: "editor"
        undoLimit: 10000 // keep a long session's history bounded

        // The gutter, left to right: line numbers (a click selects the line), bars for lines edited since
        // loading, breakpoints (a click toggles one) and a column of bookmark stars drawn in QML.
        gutterColumns: [
            LineNumberColumn { id: lineNumbers },
            ChangeColumn { id: changeBars },
            MarkerColumn {
                id: breakpoints
                width: 16
                onClicked: (line) => {
                    if (!removeMarkersAt(line))
                        addMarker(line, { color: "#e51400", barWidth: 12 })
                }
            },
            DelegateColumn {
                id: bookmarkColumn
                width: 18
                delegate: Item {
                    id: bookmark
                    required property int line
                    required property int row
                    required property int rowInLine
                    required property bool firstRow
                    required property bool current
                    // Bookmarks stay on their line numbers; they do not follow edits.
                    readonly property bool marked: window.bookmarks[line] === true
                    Text {
                        anchors.centerIn: parent
                        text: "\u2605"
                        color: bookmark.marked ? "#f5c518" : (bookmarkArea.containsMouse ? "#80808080" : "transparent")
                        font.pixelSize: Math.max(8, parent.height - 4)
                    }
                    MouseArea {
                        id: bookmarkArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            var next = Object.assign({}, window.bookmarks)
                            if (next[bookmark.line]) delete next[bookmark.line]
                            else next[bookmark.line] = true
                            window.bookmarks = next
                        }
                    }
                }
            }
        ]
        anchors.fill: parent
        anchors.rightMargin: vbar.width
        anchors.bottomMargin: hbar.visible ? hbar.height : 0

        onSaved: (path) => statusLabel.statusText = qsTr("Saved %1").arg(path)
        onSaveFailed: (error) => statusLabel.statusText = qsTr("Save failed: %1").arg(error)

        DropArea {
            anchors.fill: parent
            onDropped: (drop) => {
                if (drop.hasUrls) {
                    window.currentFile = drop.urls[0]
                    editor.load(drop.urls[0])
                }
            }
        }
    }

    DemoScrollBar {
        id: vbar
        orientation: Qt.Vertical
        anchors { top: parent.top; right: parent.right; bottom: hbar.top }
        size: editor.contentHeight > 0 ? Math.min(1, editor.height / editor.contentHeight) : 1
        position: editor.contentHeight > 0 ? editor.contentY / editor.contentHeight : 0
        onPositionChanged: if (pressed) editor.contentY = position * editor.contentHeight
    }

    DemoScrollBar {
        id: hbar
        orientation: Qt.Horizontal
        // Text wrapped to the window never scrolls sideways.
        visible: editor.wrapMode !== CodeEditor.WrapAtViewport
        anchors { left: parent.left; right: vbar.left; bottom: parent.bottom }
        anchors.leftMargin: editor.gutterWidth
        size: editor.contentWidth > 0 ? Math.min(1, (editor.width - editor.gutterWidth) / editor.contentWidth) : 1
        position: editor.contentWidth > 0 ? editor.contentX / editor.contentWidth : 0
        onPositionChanged: if (pressed) editor.contentX = position * editor.contentWidth
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open a file")
        onAccepted: {
            window.currentFile = selectedFile
            editor.load(selectedFile)
            editor.forceActiveFocus()
        }
    }
}
