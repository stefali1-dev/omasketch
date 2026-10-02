import QtQuick

// Small grey feedback line at the bottom centre: fades in, stays ~1.5 s,
// fades out. Call show() from QML, or from C++ with
// QMetaObject::invokeMethod(toast, "show", Q_ARG(QVariant, text)).
Text {
    id: toast
    objectName: "toast"

    // Fires when a message appears, so whatever shares this spot (the tool
    // label) can step aside.
    signal shown()

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 24

    color: Colors.ui
    font.family: "JetBrainsMono Nerd Font"
    font.pixelSize: 15
    opacity: 0
    visible: opacity > 0

    // The duration is picked beside each opacity change. A binding on the
    // animated opacity would read it mid-flight and swap the two (review).
    property int fadeInMs: 120
    property int fadeOutMs: 150
    property int fadeMs: fadeInMs

    Behavior on opacity {
        NumberAnimation { duration: toast.fadeMs }
    }

    function show(message) {
        toast.text = message
        toast.fadeMs = toast.fadeInMs
        toast.opacity = 1
        hold.restart()
        toast.shown()
    }

    Timer {
        id: hold
        objectName: "hold"
        interval: 1500
        onTriggered: {
            toast.fadeMs = toast.fadeOutMs
            toast.opacity = 0
        }
    }
}
