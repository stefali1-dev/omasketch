import QtQuick

// The save/open path bar (decisions.md, "Path bar"): a slim pill at the
// bottom centre with a grey mode label, the path, and grey ghost text for
// the rest of the first Tab match. Keys work like a shell: Tab completes
// and cycles, Ctrl+W kills the previous segment, Ctrl+U clears, Enter
// confirms, Esc cancels. The Loader in Main.qml creates it on first use.
Item {
    id: bar
    objectName: "pathBar"

    property string mode: "save"
    property bool shown: false         // open and taking input
    property bool noteShown: false     // the accent note right of the path
    property bool cycling: false       // Tab pressed, matches are being cycled
    property bool saveArmed: false     // "exists" shown, next Enter overwrites
    property bool updatingText: false  // programmatic text changes don't disarm
    property string ghostTail: ""      // the rest of the first Tab match

    signal confirmed(string path, string mode)

    readonly property real rightEdge: Math.max(
        input.x + input.width,
        ghost.visible ? ghost.x + ghost.width : 0,
        noteShown ? note.x + note.width : 0)
    implicitWidth: rightEdge + 14
    implicitHeight: 34

    opacity: shown ? 1 : 0
    visible: opacity > 0
    transform: Translate {
        id: slide
        y: shown ? 0 : 8
        Behavior on y { NumberAnimation { duration: 120 } }
    }
    Behavior on opacity { NumberAnimation { duration: 120 } }

    function setText(text, cursor) {
        updatingText = true
        input.text = text
        input.cursorPosition = cursor === undefined ? text.length : cursor
        updatingText = false
    }

    function show(newMode) {
        mode = newMode
        cycling = false
        saveArmed = false
        ghostTail = ""
        noteShown = false
        setText(newMode === "save"
            ? "~/Pictures/Drawings/" + Qt.formatDateTime(new Date(), "yyyy-MM-dd_HH-mm-ss") + ".png"
            : "~/Pictures/Drawings/")
        shown = true
        input.forceActiveFocus()
    }

    function hide() {
        shown = false
        input.focus = false
    }

    function completeMatch(step) {
        const result = cycling ? completer.cycle(step)
                               : completer.complete(input.text, mode === "open")
        if (result.count < 1)
            return
        cycling = true
        setText(result.text)
        ghostTail = result.ghost
    }

    function killPrev() {
        const result = completer.killPrevWord(input.text, input.cursorPosition)
        setText(result.text, result.cursor)
    }

    function confirm() {
        const absolute = completer.expand(input.text)
        if (mode === "save") {
            const path = absolute.toLowerCase().endsWith(".png") ? absolute : absolute + ".png"
            if (path !== absolute)
                setText(completer.shorten(path))
            if (!saveArmed && completer.exists(path)) {
                saveArmed = true
                note.text = "exists · enter to overwrite"
                noteShown = true
                return
            }
            bar.confirmed(path, "save")
        } else {
            if (!completer.exists(absolute)) {
                note.text = "no such file"
                noteShown = true
                return
            }
            if (completer.isDir(absolute)) {
                note.text = "is a folder"
                noteShown = true
                return
            }
            bar.confirmed(absolute, "open")
        }
        hide()
    }

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Colors.page
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.12)
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.IBeamCursor
        onClicked: input.forceActiveFocus()
    }

    Text {
        id: modeLabel
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        text: bar.mode
        color: Colors.ui
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 15
    }

    TextInput {
        id: input
        objectName: "input"
        anchors.left: modeLabel.right
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(Math.max(contentWidth + 4, 36), 560)
        clip: true
        color: Colors.ink
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 15
        selectByMouse: true

        onTextChanged: if (!bar.updatingText) {
            completer.reset()
            bar.cycling = false
            bar.saveArmed = false
            bar.ghostTail = ""
            bar.noteShown = false
        }

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                bar.completeMatch(event.key === Qt.Key_Backtab ? -1 : 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                bar.confirm()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                bar.hide()
                event.accepted = true
            } else if (event.key === Qt.Key_W && (event.modifiers & Qt.ControlModifier)) {
                bar.killPrev()
                event.accepted = true
            } else if (event.key === Qt.Key_U && (event.modifiers & Qt.ControlModifier)) {
                bar.setText("")
                event.accepted = true
            }
        }
    }

    Text {
        id: ghost
        objectName: "ghost"
        x: input.x + input.cursorRectangle.x
        anchors.verticalCenter: parent.verticalCenter
        text: bar.ghostTail
        visible: bar.ghostTail !== ""
                 && input.cursorPosition === input.text.length
                 && !bar.noteShown
        color: Qt.lighter(Colors.ui, 1.6)
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 15
    }

    Text {
        id: note
        objectName: "note"
        x: input.x + input.width + 10
        anchors.verticalCenter: parent.verticalCenter
        text: ""
        color: Colors.accent
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 15
        visible: bar.noteShown
    }
}
