import QtQuick
import QtQuick.Controls
import me.blq.qmlcodeeditor

ApplicationWindow {
    width: 960
    height: 640
    visible: true
    title: qsTr("QMLCodeEditor demo")

    CodeEditor {
        objectName: "editor"
        anchors.fill: parent
    }
}
