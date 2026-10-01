// Loads the real Main.qml module and draws through it: the startup wiring
// main.cpp does, which the hand-built pages in tst_page never exercise. This
// is where "Main.qml has no Page" regressed once.
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtTest>

#include "palette.h"
#include "page.h"
#include "tools.h"

namespace {

// The context properties main.cpp sets, so the QML resolves the same way.
QVariantMap paletteMap()
{
    return {
        {"page",   palette::page},
        {"ink",    palette::ink},
        {"red",    palette::red},
        {"blue",   palette::blue},
        {"ui",     palette::ui},
        {"accent", palette::accent},
    };
}

void draw(QQuickWindow &window, const QList<QPoint> &points)
{
    QTest::mouseMove(&window, points.first());
    QTest::mousePress(&window, Qt::LeftButton, {}, points.first());
    for (int i = 1; i < points.size(); ++i)
        QTest::mouseMove(&window, points[i]);
    QTest::mouseRelease(&window, Qt::LeftButton, {}, points.last());
}

QColor pixel(QQuickWindow &window, const QPointF &pos)
{
    const QImage image = window.grabWindow();
    const qreal dpr = image.devicePixelRatio();
    return image.pixelColor(qRound(pos.x() * dpr), qRound(pos.y() * dpr));
}

} // namespace

class AppTest : public QObject
{
    Q_OBJECT

private slots:
    void mainQmlWiresThePageAndDraws();
};

void AppTest::mainQmlWiresThePageAndDraws()
{
    // Tools first: the engine holds a raw pointer to it as a context property.
    Tools tools;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Colors", paletteMap());
    engine.rootContext()->setContextProperty("tools", &tools);
    engine.loadFromModule("Omasketch", "Main");
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);

    tools.attach(window);
    Page *page = pageIn(window);
    QVERIFY(page); // the QML-created Page must be findable and get the tools
    tools.setPage(page);

    QTest::qWait(50);
    QTest::keyClick(window, Qt::Key_D);
    QCOMPARE(tools.tool(), Tools::Draw);

    draw(*window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    QCOMPARE(page->strokes().size(), 1);
    QCOMPARE(pixel(*window, QPointF(200, 300)), palette::ink);

    // The Ctrl shortcuts reach the page through the same wiring.
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(page->strokes().size(), 0);
    QCOMPARE(pixel(*window, QPointF(200, 300)), palette::page);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(page->strokes().size(), 1);
}

QTEST_MAIN(AppTest)
#include "tst_app.moc"
