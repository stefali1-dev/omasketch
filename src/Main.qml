import QtQuick

Window {
    id: window
    visible: true
    width: 1280
    height: 800
    color: Colors.page
    title: "omasketch"

    Page {
        anchors.fill: parent
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
            pathBarLoader.active = true
            pathBarLoader.item.show(mode)
        }
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
