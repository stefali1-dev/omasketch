#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVariant>
#include <cstdio>

#include "files.h"
#include "palette.h"
#include "page.h"
#include "pathcompleter.h"
#include "tools.h"

namespace {

// Started before main runs, so the first-frame time includes Qt's startup.
QElapsedTimer processClock = [] {
    QElapsedTimer timer;
    timer.start();
    return timer;
}();
qint64 lastFrame = -1;

} // namespace

int main(int argc, char *argv[])
{
    // Graphics stay on Qt's default (OpenGL). Vulkan was tried for a faster
    // first frame, saved little, and silently broke saving: the export's
    // render-control window (files.cpp) would need a QVulkanInstance.

    // The look is fixed (palette.h, decisions.md) and there are no dialogs:
    // the host GTK theme plugin would only slow startup down (dlopen + GTK
    // init, ~50 ms to the first frame here). Fonts and colours are all set
    // by the app, so nothing on screen depends on it. IMEs are unaffected.
    qputenv("QT_QPA_PLATFORMTHEME", "");

    // Multisampling smooths the stroke edges; the frame-time bench measures
    // what it costs.
    QSurfaceFormat format;
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("omasketch");
    QGuiApplication::setDesktopFileName("omasketch");

    // The objects the QML binds to are declared before the engine, so they
    // are destroyed after it: QML teardown on close must not read a dead
    // context property (Main.qml's `tools.toolName`).
    PathCompleter completer;
    Tools tools;
    Files files;
    QQmlApplicationEngine engine;
    // Named "Colors" rather than "Palette": QtQuick has its own Palette
    // type, which would shadow a context property of that name.
    engine.rootContext()->setContextProperty("Colors", QVariantMap{
        {"page",   palette::page},
        {"ink",    palette::ink},
        {"red",    palette::red},
        {"blue",   palette::blue},
        {"ui",     palette::ui},
        {"accent", palette::accent},
    });
    engine.rootContext()->setContextProperty("completer", &completer);
    engine.rootContext()->setContextProperty("tools", &tools);
    engine.rootContext()->setContextProperty("files", &files);
    engine.loadFromModule("Omasketch", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    tools.attach(window);
    Page *page = pageIn(window);
    if (page) {
        tools.setPage(page);
        files.setPage(page);
        // Once the first frame is up (the next event-loop turn, so the
        // frame itself is not delayed), throw away a text editor: its QML
        // then sits parsed in the engine and the first real edit doesn't
        // pay the parse inside a keystroke frame.
        QObject::connect(window, &QQuickWindow::frameSwapped, window,
                         [page] { QTimer::singleShot(0, page, [page] { page->warmTextEditor(); }); },
                         Qt::SingleShotConnection);
    } else {
        qWarning("omasketch: Main.qml has no Page");
    }

    // Files: the shortcuts, toasts, autosave on close, and the open file
    // from the command line once the first frame is up. The path bar's
    // confirmations arrive through Main.qml (Loader.onLoaded).
    QObject::connect(&tools, &Tools::saveRequested, &files, &Files::save);
    QObject::connect(&tools, &Tools::copyRequested, &files, &Files::copySelection);
    QObject::connect(&tools, &Tools::pasteRequested, &files, &Files::paste);
    QObject::connect(&files, &Files::toastRequested, window,
                     [window](const QString &message) {
                         if (auto *toast = window->findChild<QObject *>("toast"))
                             QMetaObject::invokeMethod(toast, "show",
                                                       Q_ARG(QVariant, message));
                     });
    QObject::connect(window, &QQuickWindow::closing, &files, &Files::appClosing);
    // `omasketch file.png` opens it once the first frame is up. A first
    // argument starting with - is a flag (Qt's own --platform, --help…),
    // not a file.
    if (argc > 1 && !QGuiApplication::arguments().value(1).startsWith('-')) {
        const QString path = QFileInfo(QGuiApplication::arguments().value(1))
                                 .absoluteFilePath();
        QObject::connect(window, &QQuickWindow::frameSwapped, &files,
                         [&files, path] { files.openStartupFile(path); },
                         Qt::SingleShotConnection);
    }

    if (qEnvironmentVariableIsSet("OMASKETCH_TIMING")) {
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] {
            const qint64 now = processClock.elapsed();
            if (lastFrame < 0)
                std::fprintf(stderr, "omasketch: first frame %lld ms after start\n",
                             static_cast<long long>(now));
            else if (now - lastFrame < 250)
                std::fprintf(stderr, "omasketch: frame +%lld ms\n",
                             static_cast<long long>(now - lastFrame));
            lastFrame = now;
        });
    }

    return app.exec();
}
