import QtQuick

Window {
    id: window
    visible: true
    width: 1280
    height: 800
    color: Colors.page
    title: "omasketch"

    Page {
        id: page
        anchors.fill: parent
    }

    // A click on the page leaves the path bar, like Esc does. It retires the
    // key hint too, as does anything landing on the page (an open, a paste).
    Connections {
        target: page
        function onPressed() {
            if (pathBarLoader.active)
                pathBarLoader.item.hide()
            keyHint.retired = true
        }
        function onItemsChanged() { keyHint.retired = true }
    }

    // A fresh launch names the main keys in the middle of the blank page.
    Text {
        id: keyHint
        objectName: "keyHint"
        property bool retired: false
        anchors.centerIn: parent
        text: "d draw · a arrow · t text · v select · ? all keys"
        color: Qt.lighter(Colors.ui, 1.6)
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 15
        opacity: retired ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    // The way in for the mouse: a faint `?` in the corner.
    Text {
        objectName: "keysButton"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        text: "?"
        color: Colors.ui
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 16
        opacity: keysButtonArea.containsMouse ? 1 : 0.5
        Behavior on opacity { NumberAnimation { duration: 120 } }

        MouseArea {
            id: keysButtonArea
            anchors.fill: parent
            anchors.margins: -10
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: window.toggleKeys()
        }
    }

    // Built on the first `?`, so startup doesn't pay for it.
    Loader {
        id: keysLoader
        anchors.fill: parent
        active: false
        sourceComponent: KeysCard { }
    }

    // The card takes the keys, so a text edit or the path bar ends first.
    function toggleKeys() {
        page.commitEditing()
        if (pathBarLoader.active)
            pathBarLoader.item.hide()
        keysLoader.active = true
        keysLoader.item.shown = !keysLoader.item.shown
        keyHint.retired = true
    }

    Text {
        id: toolLabel
        objectName: "toolLabel"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
        text: tools.toolName
        color: Colors.ui
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 16
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    Timer {
        id: hideToolLabel
        interval: 1000
        onTriggered: toolLabel.opacity = 0
    }

    Toast { id: toast }

    Loader {
        id: pathBarLoader
        objectName: "pathBarLoader"
        active: false
        sourceComponent: PathBar { }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 60
        // The confirmed path goes to Files (main.cpp wires the context
        // property), which saves or opens and shows the result as a toast.
        onLoaded: item.confirmed.connect(
            (path, mode) => files.confirm(path, mode))
    }

    // A PNG dropped from a file manager joins the page as an image item.
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            for (const url of drop.urls)
                if (url.toString().toLowerCase().endsWith(".png"))
                    files.openDrop(url)
        }
    }

    Connections {
        target: tools
        function onToolChanged() {
            toolLabel.opacity = 1
            hideToolLabel.restart()
        }
        function onPathBarRequested(mode) {
            if (keysLoader.active)
                keysLoader.item.shown = false
            pathBarLoader.active = true
            pathBarLoader.item.show(mode)
        }
        function onKeysRequested() { window.toggleKeys() }
    }

    // The toast and the label share the bottom centre: while a toast shows,
    // the label steps aside.
    Connections {
        target: toast
        function onShown() {
            toolLabel.opacity = 0
            hideToolLabel.stop()
        }
    }
}
