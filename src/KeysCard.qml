import QtQuick

// The keys at a glance, centred over the page: `?` (Tools) or the corner
// button toggles it. Any other key puts it away and still does its job, so
// `d` closes the card and picks the pencil; a click only puts it away.
Item {
    id: keysCard
    objectName: "keysCard"

    property bool shown: false

    anchors.fill: parent
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    // Focus while shown, so the next key reaches the card after Tools.
    onShownChanged: if (shown) forceActiveFocus(); else focus = false
    // `?` itself toggles in Tools; Shift is only on the way to it.
    Keys.onPressed: (event) => {
        if (event.key !== Qt.Key_Question && event.key !== Qt.Key_Shift)
            shown = false
    }

    // Over the whole page: the click that closes the card draws nothing.
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.ArrowCursor
        onPressed: keysCard.shown = false
    }

    Rectangle {
        anchors.centerIn: parent
        width: grid.width + 56
        height: grid.height + 48
        radius: 8
        color: Colors.page
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.12)

        Grid {
            id: grid
            anchors.centerIn: parent
            columns: 2
            columnSpacing: 24
            rowSpacing: 6

            Repeater {
                model: [
                    "d", "draw",
                    "a", "arrow",
                    "t", "text",
                    "e", "eraser",
                    "v", "select",
                    "1 2 3", "black, red, blue",
                    "space drag", "pan",
                    "ctrl scroll", "zoom",
                    "ctrl 0", "back to the drawing",
                    "ctrl a", "select all",
                    "del", "delete the selection",
                    "ctrl z", "undo, + shift: redo",
                    "ctrl c", "copy as PNG",
                    "ctrl v", "paste an image",
                    "ctrl s", "save, + shift: save as",
                    "ctrl o", "open a PNG",
                    "ctrl n", "fresh page",
                    "esc", "stop typing, deselect",
                ]
                Text {
                    required property string modelData
                    required property int index
                    text: modelData
                    color: index % 2 === 0 ? Colors.ink : Colors.ui
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 14
                }
            }
        }
    }
}
