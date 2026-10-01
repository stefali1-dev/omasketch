#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <cstdio>

#include "palette.h"
#include "page.h"
#include "pathcompleter.h"
#include "tools.h"

namespace {

// Bridges the lazily created path bar to the toast. A small QObject because
// QML-declared signals have no member pointer a lambda could connect with.
class ToastBridge : public QObject
{
    Q_OBJECT
public:
    ToastBridge(QQuickItem *toast, QQuickItem *loader, const PathCompleter *completer, QObject *parent)
        : QObject(parent), m_toast(toast), m_loader(loader), m_completer(completer)
    {
    }

public slots:
    void pathBarReady()
    {
        auto *pathBar = qvariant_cast<QQuickItem *>(m_loader->property("item"));
        if (pathBar)
            QObject::connect(pathBar, SIGNAL(confirmed(QString,QString)),
                             this, SLOT(confirmed(QString,QString)));
    }

    void confirmed(const QString &path, const QString &mode)
    {
        QMetaObject::invokeMethod(m_toast, "show",
            Q_ARG(QVariant, mode + " → " + m_completer->shorten(path)));
    }

private:
    QQuickItem *m_toast;
    QQuickItem *m_loader;
    const PathCompleter *m_completer;
};

} // namespace

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
    // Multisampling smooths the stroke edges; the frame-time bench measures
    // what it costs.
    QSurfaceFormat format;
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("omasketch");
    QGuiApplication::setDesktopFileName("omasketch");

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
    PathCompleter completer;
    engine.rootContext()->setContextProperty("completer", &completer);
    Tools tools;
    engine.rootContext()->setContextProperty("tools", &tools);
    engine.loadFromModule("Omasketch", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    tools.attach(window);
    if (auto *page = pageIn(window))
        tools.setPage(page);
    else
        qWarning("omasketch: Main.qml has no Page");

    // The path bar is created lazily by its Loader; connect its result to
    // the toast the first time it comes into being.
    auto *toast = window->findChild<QQuickItem *>("toast");
    auto *loader = window->findChild<QQuickItem *>("pathBarLoader");
    if (toast && loader) {
        auto *bridge = new ToastBridge(toast, loader, &completer, window);
        QObject::connect(loader, SIGNAL(itemChanged()), bridge, SLOT(pathBarReady()));
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

#include "main.moc"
