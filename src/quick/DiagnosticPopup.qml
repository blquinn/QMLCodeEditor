import QtQuick

// The default hover popup for diagnostics (DIAG-04). A popup delegate is any Item with two required
// properties: `diagnostics`, a list of LSP diagnostic objects (message, severity, code, source,
// relatedInformation, ...) most severe first, and `editor`, the CodeEditor the popup belongs to. The
// editor sizes the popup from its width and height (implicit ones when those are unset) and places
// it next to the text. The text can be selected with the mouse and copied (Ctrl+C); the popup then
// takes the keyboard focus and stays until Escape or a click in the editor.
Rectangle {
    id: root
    objectName: "diagnosticPopup"

    required property var diagnostics
    required property Item editor

    readonly property color base: editor.theme.background
    readonly property color ink: editor.theme.foreground
    readonly property real maxTextWidth: 480

    color: base.hslLightness < 0.5 ? Qt.lighter(base, 1.4) : Qt.darker(base, 1.05)
    border.color: Qt.rgba(ink.r, ink.g, ink.b, 0.25)
    radius: 4
    implicitWidth: column.implicitWidth + 2 * column.x
    implicitHeight: column.implicitHeight + 2 * column.y

    Column {
        id: column
        x: 8
        y: 6
        spacing: 6

        Repeater {
            model: root.diagnostics
            delegate: Row {
                id: entry
                required property var modelData
                spacing: 6

                Rectangle {
                    width: 3
                    height: text.height
                    radius: 1
                    color: root.editor.theme.severityColor(entry.modelData.severity)
                }
                Column {
                    TextEdit {
                        id: text
                        objectName: "diagnosticMessage"
                        width: Math.min(implicitWidth, root.maxTextWidth)
                        text: entry.modelData.message
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        color: root.ink
                        selectionColor: root.editor.theme.selection
                        selectedTextColor: root.ink
                        font: root.editor.font
                        Keys.onEscapePressed: root.close()
                    }
                    TextEdit {
                        id: details
                        objectName: "diagnosticDetails"
                        visible: text2.length > 0
                        readonly property string text2: {
                            const source = entry.modelData.source || "";
                            const code = entry.modelData.code;
                            const codeText = code === undefined || code === null || code === "" ? "" : "(" + code + ")";
                            return source + codeText;
                        }
                        text: text2
                        readOnly: true
                        selectByMouse: true
                        color: Qt.rgba(root.ink.r, root.ink.g, root.ink.b, 0.6)
                        selectionColor: root.editor.theme.selection
                        selectedTextColor: root.ink
                        font.family: root.editor.font.family
                        font.pixelSize: Math.max(8, root.editor.font.pixelSize > 0 ? root.editor.font.pixelSize - 2 : root.editor.font.pointSize * 1.2)
                        Keys.onEscapePressed: root.close()
                    }
                }
            }
        }
    }

    // Closes the popup and gives the keyboard back to the editor.
    function close() {
        editor.hidePopup()
        editor.forceActiveFocus()
    }
}
