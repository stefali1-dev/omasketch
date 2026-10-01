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
        // The confirmed path shows as toast text until the later files
        // task connects the signal in C++.
        onLoaded: item.confirmed.connect(
            (path, mode) => toast.show(mode + " → " + completer.shorten(path)))
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
}
