import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import me.blq.qmlcodeeditor

ApplicationWindow {
    id: window

    // Set from the command line by main.cpp.
    property url initialFile

    width: 1100
    height: 720
    visible: true
    title: qsTr("QMLCodeEditor demo")

    Component.onCompleted: if (initialFile.toString() !== "") editor.load(initialFile)

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 12

            ToolButton {
                text: qsTr("Open…")
                onClicked: openDialog.open()
            }
            CheckBox {
                id: darkMode
                text: qsTr("Dark")
                checked: true
                onToggled: editor.theme.applyPreset(checked ? "dark" : "light")
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
            Label {
                text: qsTr("%1 lines").arg(editor.lineCount.toLocaleString())
            }
        }
    }

    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            Label {
                text: editor.loading ? qsTr("Loading… %1%").arg(Math.round(editor.loadProgress * 100))
                                     : qsTr("Ready")
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("%1 fps").arg(fps.value.toFixed(0)) }
        }
    }

    // Frame rate from the interval between presented frames, smoothed over about half a second.
    QtObject {
        id: fps
        property real value: 0
        property double last: 0
        property real smoothed: 0
    }
    Connections {
        target: window
        function onFrameSwapped() {
            var now = Date.now()
            if (fps.last > 0) {
                var dt = now - fps.last
                if (dt > 0) {
                    fps.smoothed = fps.smoothed * 0.9 + (1000 / dt) * 0.1
                    fps.value = fps.smoothed
                }
            }
            fps.last = now
        }
    }

    CodeEditor {
        id: editor
        objectName: "editor"
        anchors.fill: parent
        anchors.rightMargin: vbar.width
        anchors.bottomMargin: hbar.height

        DropArea {
            anchors.fill: parent
            onDropped: (drop) => {
                if (drop.hasUrls)
                    editor.load(drop.urls[0])
            }
        }
    }

    ScrollBar {
        id: vbar
        orientation: Qt.Vertical
        anchors { top: parent.top; right: parent.right; bottom: hbar.top }
        size: editor.contentHeight > 0 ? Math.min(1, editor.height / editor.contentHeight) : 1
        position: editor.contentHeight > 0 ? editor.contentY / editor.contentHeight : 0
        policy: ScrollBar.AlwaysOn
        onPositionChanged: if (pressed) editor.contentY = position * editor.contentHeight
    }

    ScrollBar {
        id: hbar
        orientation: Qt.Horizontal
        anchors { left: parent.left; right: vbar.left; bottom: parent.bottom }
        size: editor.contentWidth > 0 ? Math.min(1, editor.width / editor.contentWidth) : 1
        position: editor.contentWidth > 0 ? editor.contentX / editor.contentWidth : 0
        policy: ScrollBar.AlwaysOn
        onPositionChanged: if (pressed) editor.contentX = position * editor.contentWidth
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open a file")
        onAccepted: editor.load(selectedFile)
    }
}
