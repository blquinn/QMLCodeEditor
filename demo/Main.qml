import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import me.blq.qmlcodeeditor
import me.blq.qmlcodeeditor.syntax

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

    // A labelled spin box as a menu entry. It is a plain item, not a MenuItem, so using it does not close the menu.
    component MenuSpin: Item {
        id: spinRow
        property alias label: caption.text
        property alias from: box.from
        property alias to: box.to
        property alias value: box.value
        signal modified(int value)
        // A Menu sizes itself from its text entries only, so menus holding one of these set their own width.
        width: parent ? parent.width : implicitWidth
        implicitWidth: row.implicitWidth + 24
        implicitHeight: 48
        RowLayout {
            id: row
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 24
            Label {
                id: caption
                Layout.fillWidth: true
                Layout.minimumWidth: implicitWidth
                enabled: spinRow.enabled
            }
            SpinBox {
                id: box
                enabled: spinRow.enabled
                onValueModified: spinRow.modified(value)
            }
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

    // Diagnostics in LSP's shape, as a language server would publish them: `count` of them, one every
    // few lines through the whole file, over the first characters of the line (the editor clamps
    // ranges to the text).
    function sampleDiagnostics(count) {
        const lines = editor.lineCount
        const step = Math.max(1, Math.floor(lines / count))
        const kinds = [qsTr("error"), qsTr("warning"), qsTr("information"), qsTr("hint")]
        const list = []
        for (let i = 0; i < count && i * step < lines; ++i) {
            const line = i * step
            list.push({
                range: { start: { line: line, character: 0 }, end: { line: line, character: 6 } },
                severity: 1 + i % 4,
                message: qsTr("Sample %1 on line %2: something is not quite right here").arg(kinds[i % 4]).arg(line + 1),
                source: "demo",
                code: 100 + i % 4
            })
        }
        editor.setDiagnostics(list)
        statusLabel.statusText = qsTr("%1 diagnostics").arg(list.length.toLocaleString())
    }

    // Inlay hints around the cursor: a type after the third character and a parameter name before the
    // eighth, on forty lines.
    function sampleHints() {
        const first = Math.max(0, editor.cursorLine - 20)
        const list = []
        for (let line = first; line < first + 40 && line < editor.lineCount; ++line) {
            list.push({ position: { line: line, character: 3 }, label: ": text", kind: 1 })
            list.push({ position: { line: line, character: 8 }, label: "arg:", kind: 2 })
        }
        editor.setInlayHints(list)
    }

    // Everything but the editor itself lives in the menus.
    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            MenuItem {
                text: qsTr("&Open…")
                onTriggered: openDialog.open()
            }
            MenuItem {
                text: qsTr("&Save")
                enabled: window.currentFile.toString() !== ""
                onTriggered: editor.save(window.currentFile)
            }
        }
        Menu {
            title: qsTr("&Edit")
            MenuItem {
                text: qsTr("&Undo")
                enabled: editor.canUndo
                onTriggered: editor.undo()
            }
            MenuItem {
                text: qsTr("&Redo")
                enabled: editor.canRedo
                onTriggered: editor.redo()
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Cu&t"); onTriggered: editor.cut() }
            MenuItem { text: qsTr("&Copy"); onTriggered: editor.copy() }
            MenuItem { text: qsTr("&Paste"); onTriggered: editor.paste() }
            MenuSeparator {}
            MenuItem { text: qsTr("Select &All"); onTriggered: editor.selectAll() }
        }
        Menu {
            title: qsTr("&Selection")
            MenuItem { text: qsTr("Add cursor &above\tCtrl+Alt+Up"); onTriggered: editor.addCursorAbove() }
            MenuItem { text: qsTr("Add cursor &below\tCtrl+Alt+Down"); onTriggered: editor.addCursorBelow() }
            MenuItem { text: qsTr("Add &next occurrence\tCtrl+D"); onTriggered: editor.addNextOccurrence() }
            MenuItem { text: qsTr("Select all &occurrences\tCtrl+Shift+L"); onTriggered: editor.selectAllOccurrences() }
            MenuSeparator {}
            MenuItem {
                text: qsTr("&Single cursor\tEsc")
                enabled: editor.selectionCount > 1
                onTriggered: editor.collapseSelections()
            }
        }
        Menu {
            title: qsTr("&View")
            width: 300 // room for a label and a spin box
            MenuItem {
                text: qsTr("Dark theme")
                checkable: true
                checked: window.darkMode
                onToggled: {
                    window.darkMode = checked
                    editor.theme.applyPreset(checked ? "dark" : "light")
                }
            }
            MenuItem {
                text: qsTr("Show whitespace")
                checkable: true
                onToggled: editor.showWhitespace = checked
            }
            MenuItem {
                text: qsTr("Read-only")
                checkable: true
                onToggled: editor.readOnly = checked
            }
            MenuSeparator {}
            MenuSpin {
                label: qsTr("Font size")
                from: 6; to: 48; value: editor.font.pointSize
                onModified: (v) => {
                    var f = editor.font
                    f.pointSize = v
                    editor.font = f
                }
            }
        }
        Menu {
            title: qsTr("&Indentation")
            width: 300 // room for a label and a spin box
            MenuItem {
                text: qsTr("Detect from file")
                checkable: true
                checked: editor.detectIndentation
                onToggled: editor.detectIndentation = checked
            }
            MenuItem {
                text: qsTr("Indent with tabs")
                checkable: true
                checked: !editor.insertSpaces
                onToggled: editor.insertSpaces = !checked
            }
            MenuItem {
                text: qsTr("Auto-close brackets")
                checkable: true
                checked: editor.autoClose
                onToggled: editor.autoClose = checked
            }
            MenuItem {
                text: qsTr("Highlight matching brackets")
                checkable: true
                checked: editor.matchBrackets
                onToggled: editor.matchBrackets = checked
            }
            MenuItem {
                text: qsTr("Show indent guides")
                checkable: true
                checked: editor.showIndentGuides
                onToggled: editor.showIndentGuides = checked
            }
            MenuSeparator {}
            MenuSpin {
                label: qsTr("Indent width")
                from: 1; to: 16; value: editor.indentWidth
                onModified: (v) => editor.indentWidth = v
            }
            MenuSpin {
                label: qsTr("Tab width")
                from: 1; to: 16; value: editor.tabWidth
                onModified: (v) => editor.tabWidth = v
            }
        }
        Menu {
            title: qsTr("&Wrapping")
            width: 300 // room for a label and a spin box
            MenuItem {
                text: qsTr("No wrap")
                checkable: true; autoExclusive: true
                checked: editor.wrapMode === CodeEditor.NoWrap
                onTriggered: editor.wrapMode = CodeEditor.NoWrap
            }
            MenuItem {
                text: qsTr("Wrap at window")
                checkable: true; autoExclusive: true
                checked: editor.wrapMode === CodeEditor.WrapAtViewport
                onTriggered: editor.wrapMode = CodeEditor.WrapAtViewport
            }
            MenuItem {
                text: qsTr("Wrap at column")
                checkable: true; autoExclusive: true
                checked: editor.wrapMode === CodeEditor.WrapAtColumn
                onTriggered: editor.wrapMode = CodeEditor.WrapAtColumn
            }
            MenuSeparator {}
            MenuSpin {
                label: qsTr("Wrap column")
                enabled: editor.wrapMode === CodeEditor.WrapAtColumn
                from: 10; to: 400; value: editor.wrapColumn
                onModified: (v) => editor.wrapColumn = v
            }
            MenuItem {
                text: qsTr("Break at words")
                checkable: true
                checked: editor.wordWrap
                onToggled: editor.wordWrap = checked
            }
            MenuItem {
                text: qsTr("Hanging indent")
                checkable: true
                checked: editor.wrapIndent
                onToggled: editor.wrapIndent = checked
            }
            MenuSpin {
                label: qsTr("Extra indent")
                from: 0; to: 16; value: editor.wrapIndentExtra
                onModified: (v) => editor.wrapIndentExtra = v
            }
        }
        Menu {
            id: languageMenu
            title: qsTr("&Language")
            MenuItem {
                text: qsTr("Auto-detect")
                checkable: true; autoExclusive: true
                checked: highlighter.language === ""
                onTriggered: highlighter.language = ""
            }
            MenuItem {
                text: qsTr("Plain text")
                checkable: true; autoExclusive: true
                checked: highlighter.language === "plain"
                onTriggered: highlighter.language = "plain"
            }
            MenuSeparator {}
            Instantiator {
                model: highlighter.availableLanguages()
                delegate: MenuItem {
                    required property string modelData
                    text: highlighter.languageName(modelData)
                    checkable: true; autoExclusive: true
                    checked: highlighter.language === modelData
                    onTriggered: highlighter.language = modelData
                }
                onObjectAdded: (index, object) => languageMenu.insertItem(index + 3, object)
                onObjectRemoved: (index, object) => languageMenu.removeItem(object)
            }
        }
        Menu {
            title: qsTr("F&old")
            MenuItem { text: qsTr("&Fold at cursor"); onTriggered: editor.foldAtCursor() }
            MenuItem { text: qsTr("&Unfold at cursor"); onTriggered: editor.unfoldAtCursor() }
            MenuSeparator {}
            MenuItem { text: qsTr("Fold &all"); onTriggered: editor.foldAll() }
            MenuItem { text: qsTr("U&nfold all"); onTriggered: editor.unfoldAll() }
            MenuSeparator {}
            MenuItem { text: qsTr("Fold level &1"); onTriggered: editor.foldToLevel(1) }
            MenuItem { text: qsTr("Fold level &2"); onTriggered: editor.foldToLevel(2) }
            MenuItem { text: qsTr("Fold level &3"); onTriggered: editor.foldToLevel(3) }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Open a fold when the cursor enters it")
                checkable: true
                checked: editor.foldCursorPolicy === CodeEditor.UnfoldOnEnter
                onToggled: editor.foldCursorPolicy = checked ? CodeEditor.UnfoldOnEnter : CodeEditor.SkipFolds
            }
            MenuItem {
                text: qsTr("Fold markers")
                checkable: true
                checked: foldMarkers.visible
                onToggled: foldMarkers.visible = checked
            }
        }
        Menu {
            title: qsTr("Diagno&stics")
            MenuItem { text: qsTr("Sample diagnostics (every few lines)"); onTriggered: window.sampleDiagnostics(Math.max(1, Math.round(editor.lineCount / 15))) }
            MenuItem { text: qsTr("100,000 diagnostics"); onTriggered: window.sampleDiagnostics(100000) }
            MenuItem { text: qsTr("Clear diagnostics"); onTriggered: editor.clearDiagnostics() }
            MenuSeparator {}
            MenuItem { text: qsTr("Next diagnostic\tF8"); enabled: editor.diagnosticCount > 0; onTriggered: editor.gotoNextDiagnostic() }
            MenuItem { text: qsTr("Previous diagnostic\tShift+F8"); enabled: editor.diagnosticCount > 0; onTriggered: editor.gotoPreviousDiagnostic() }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Messages at the end of the line")
                checkable: true
                checked: editor.diagnosticMessages === CodeEditor.EndOfLineMessages
                onToggled: editor.diagnosticMessages = checked ? CodeEditor.EndOfLineMessages : CodeEditor.NoMessages
            }
            MenuItem {
                text: qsTr("Popups on hover")
                checkable: true
                checked: editor.diagnosticPopups
                onToggled: editor.diagnosticPopups = checked
            }
            MenuItem {
                text: qsTr("Icons in the gutter")
                checkable: true
                checked: diagnosticIcons.visible
                onToggled: diagnosticIcons.visible = checked
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Sample inlay hints around the cursor"); onTriggered: window.sampleHints() }
            MenuItem { text: qsTr("Clear inlay hints"); onTriggered: editor.clearInlayHints() }
        }
        Menu {
            title: qsTr("&Gutter")
            MenuItem {
                text: qsTr("Line numbers")
                checkable: true
                checked: lineNumbers.visible
                onToggled: lineNumbers.visible = checked
            }
            MenuItem {
                text: qsTr("Absolute numbers")
                enabled: lineNumbers.visible
                checkable: true; autoExclusive: true
                checked: lineNumbers.mode === LineNumberColumn.Absolute
                onTriggered: lineNumbers.mode = LineNumberColumn.Absolute
            }
            MenuItem {
                text: qsTr("Relative numbers")
                enabled: lineNumbers.visible
                checkable: true; autoExclusive: true
                checked: lineNumbers.mode === LineNumberColumn.Relative
                onTriggered: lineNumbers.mode = LineNumberColumn.Relative
            }
            MenuItem {
                text: qsTr("Hybrid numbers")
                enabled: lineNumbers.visible
                checkable: true; autoExclusive: true
                checked: lineNumbers.mode === LineNumberColumn.Hybrid
                onTriggered: lineNumbers.mode = LineNumberColumn.Hybrid
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Change bars")
                checkable: true
                checked: changeBars.visible
                onToggled: changeBars.visible = checked
            }
            MenuItem {
                text: qsTr("Breakpoints")
                checkable: true
                checked: breakpoints.visible
                onToggled: breakpoints.visible = checked
            }
            MenuItem {
                text: qsTr("Bookmarks")
                checkable: true
                checked: bookmarkColumn.visible
                onToggled: bookmarkColumn.visible = checked
            }
        }
    }

    footer: ColumnLayout {
        spacing: 0

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
                    text: highlighter.detectedLanguage !== "" ? highlighter.languageName(highlighter.detectedLanguage)
                                                              : qsTr("Plain text")
                }
                Label {
                    visible: editor.diagnosticCount > 0
                    text: qsTr("%1 problems").arg(editor.diagnosticCount.toLocaleString())
                }
                Label { text: qsTr("%1 lines").arg(editor.lineCount.toLocaleString()) }
                Label {
                    text: qsTr("Ln %1, Col %2").arg(editor.cursorLine + 1).arg(editor.cursorColumn + 1) +
                          (editor.selectionCount > 1 ? qsTr(" (%1 cursors)").arg(editor.selectionCount) : "")
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

        // Fold ranges come from the grammar (and from indentation where the language has none).
        foldProvider: highlighter.folds

        // tree-sitter highlighting; the language follows the file name (or its shebang) unless chosen
        // in the Language menu.
        highlighter: SyntaxHighlighter {
            id: highlighter
            fileName: window.currentFile.toString()
        }

        // The gutter, left to right: line numbers (a click selects the line), bars for lines edited since
        // loading, breakpoints (a click toggles one), diagnostic icons (hover for the message), fold
        // chevrons and a column of bookmark stars drawn in QML.
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
            DecorationColumn { id: diagnosticIcons },
            FoldColumn { id: foldMarkers },
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
