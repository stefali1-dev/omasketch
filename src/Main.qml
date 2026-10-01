import QtQuick

Window {
    id: window
    visible: true
    width: 1280
    height: 800
    color: Palette.page
    title: "omasketch"

    Text {
        id: toolLabel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
        text: tools.toolName
        color: Palette.ui
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

    Connections {
        target: tools
        function onToolChanged() {
            toolLabel.opacity = 1
            hideToolLabel.restart()
        }
    }
}
