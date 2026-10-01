import QtQuick

// Small grey feedback line at the bottom centre: fades in, stays ~1.5 s,
// fades out. Call show() from QML, or from C++ with
// QMetaObject::invokeMethod(toast, "show", Q_ARG(QVariant, text)).
Text {
    id: toast
    objectName: "toast"

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 24

    color: Colors.ui
    font.family: "JetBrainsMono Nerd Font"
    font.pixelSize: 15
    opacity: 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: toast.opacity > 0 ? 120 : 250 }
    }

    function show(message) {
        toast.text = message
        toast.opacity = 1
        hold.restart()
    }

    Timer {
        id: hold
        interval: 1500
        onTriggered: toast.opacity = 0
    }
}
