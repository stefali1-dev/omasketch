#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <cstdio>

#include "palette.h"
#include "page.h"
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
    // Multisampling smooths the stroke edges; the frame-time bench measures
    // what it costs.
    QSurfaceFormat format;
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("omasketch");
    QGuiApplication::setDesktopFileName("omasketch");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Palette", QVariantMap{
        {"page",   palette::page},
        {"ink",    palette::ink},
        {"red",    palette::red},
        {"blue",   palette::blue},
        {"ui",     palette::ui},
        {"accent", palette::accent},
    });
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
