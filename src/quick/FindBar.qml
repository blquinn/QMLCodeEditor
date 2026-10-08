import QtQuick

// A find and replace bar for a CodeEditor (API-04). The editor ships it but never creates it: put
// one next to the editor (it floats at the top right of whatever it is anchored in), and call open()
// from a shortcut:
//
//     CodeEditor { id: editor }
//     FindBar { editor: editor }
//     Shortcut { sequences: [StandardKey.Find]; onActivated: findBar.open(false) }
//
// Everything it does goes through `editor.find`, so a host that wants another look can build its own
// on that object. Enter and Shift+Enter move to the next and previous match, Ctrl+Alt+Enter
// replaces all, Escape closes the bar and returns the focus to the editor.
Rectangle {
    id: root
    objectName: "findBar"

    required property Item editor
    property bool replaceVisible: false

    readonly property var find: editor.find
    readonly property color base: editor.theme.background
    readonly property color ink: editor.theme.foreground
    readonly property real fieldWidth: 220

    visible: find.active
    anchors.top: parent ? parent.top : undefined
    anchors.right: parent ? parent.right : undefined
    anchors.margins: 12
    z: 10
    color: base.hslLightness < 0.5 ? Qt.lighter(base, 1.4) : Qt.darker(base, 1.05)
    border.color: Qt.rgba(ink.r, ink.g, ink.b, 0.25)
    radius: 4
    implicitWidth: column.implicitWidth + 2 * column.x
    implicitHeight: column.implicitHeight + 2 * column.y

    // Shows the bar with the selection (or the word at the cursor) as the query.
    function open(withReplace) {
        replaceVisible = withReplace;
        if (!find.active)
            find.useSelection();
        find.active = true;
        (withReplace && find.text !== "" ? replaceField : findField).forceFocus();
    }

    function close() {
        find.active = false;
        editor.forceActiveFocus();
    }

    // A line of text with a box around it.
    component Field: Rectangle {
        id: field
        property alias text: input.text
        property alias placeholder: hint.text
        property alias input: input
        signal accepted(int modifiers)
        function forceFocus() { input.forceActiveFocus(); input.selectAll(); }

        width: root.fieldWidth
        height: input.implicitHeight + 8
        color: root.base
        radius: 3
        border.color: input.activeFocus ? root.editor.theme.cursor : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.25)

        TextInput {
            id: input
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            color: root.ink
            selectionColor: root.editor.theme.selection
            selectedTextColor: root.ink
            font: root.editor.font
            selectByMouse: true
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) {
                    root.close();
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    field.accepted(event.modifiers);
                    event.accepted = true;
                }
            }
        }
        Text {
            id: hint
            anchors.fill: parent
            anchors.margins: 4
            visible: input.text === "" && !input.preeditText
            color: Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.4)
            font: root.editor.font
        }
    }

    // A small clickable label.
    component Button: Rectangle {
        id: button
        property alias label: caption.text
        property bool checked: false
        property bool toggle: false
        signal clicked

        implicitWidth: Math.max(caption.implicitWidth + 12, height)
        implicitHeight: caption.implicitHeight + 8
        radius: 3
        color: checked ? Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.25)
                       : area.containsMouse ? Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.12) : "transparent"
        Text {
            id: caption
            anchors.centerIn: parent
            color: root.ink
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onClicked: button.clicked()
        }
    }

    Column {
        id: column
        x: 8
        y: 6
        spacing: 4

        Row {
            spacing: 4
            Field {
                id: findField
                objectName: "findField"
                placeholder: qsTr("Find")
                onTextChanged: if (root.find.text !== text) root.find.text = text
                onAccepted: modifiers => {
                    if ((modifiers & Qt.ControlModifier) && (modifiers & Qt.AltModifier))
                        root.find.replaceAll();
                    else if (modifiers & Qt.ShiftModifier)
                        root.find.previous();
                    else
                        root.find.next();
                }
                Connections {
                    target: root.find
                    function onTextChanged() { if (findField.text !== root.find.text) findField.text = root.find.text }
                }
            }
            Button {
                label: "Aa"
                objectName: "caseButton"
                checked: root.find.caseSensitive
                onClicked: root.find.caseSensitive = !root.find.caseSensitive
            }
            Button {
                label: "W"
                objectName: "wordButton"
                checked: root.find.wholeWord
                onClicked: root.find.wholeWord = !root.find.wholeWord
            }
            Button {
                label: ".*"
                objectName: "regexButton"
                checked: root.find.regex
                onClicked: root.find.regex = !root.find.regex
            }
            Text {
                objectName: "findStatus"
                anchors.verticalCenter: parent.verticalCenter
                width: 90
                elide: Text.ElideRight
                color: root.find.error !== "" ? "#e06c75" : Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.7)
                font.pixelSize: root.editor.font.pixelSize > 0 ? root.editor.font.pixelSize - 1 : 12
                text: {
                    if (root.find.error !== "")
                        return root.find.error;
                    if (root.find.text === "")
                        return "";
                    if (root.find.busy && root.find.matchCount === 0)
                        return "…";
                    if (root.find.matchCount === 0)
                        return qsTr("No results");
                    const more = root.find.capped ? "+" : "";
                    return (root.find.currentIndex >= 0 ? root.find.currentIndex + 1 : "?") + "/" + root.find.matchCount + more;
                }
            }
            Button { label: "↑"; objectName: "previousButton"; onClicked: root.find.previous() }
            Button { label: "↓"; objectName: "nextButton"; onClicked: root.find.next() }
            Button { label: "⇄"; objectName: "replaceToggle"; checked: root.replaceVisible; onClicked: root.replaceVisible = !root.replaceVisible }
            Button { label: "✕"; objectName: "closeButton"; onClicked: root.close() }
        }

        Row {
            visible: root.replaceVisible
            spacing: 4
            Field {
                id: replaceField
                objectName: "replaceField"
                placeholder: qsTr("Replace")
                onTextChanged: root.find.replacement = text
                onAccepted: modifiers => {
                    if ((modifiers & Qt.ControlModifier) && (modifiers & Qt.AltModifier))
                        root.find.replaceAll();
                    else
                        root.find.replace();
                }
            }
            Button { label: qsTr("Replace"); objectName: "replaceButton"; onClicked: root.find.replace() }
            Button { label: qsTr("All"); objectName: "replaceAllButton"; onClicked: root.find.replaceAll() }
        }
    }
}
