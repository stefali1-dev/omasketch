// Loads the real Main.qml module and draws through it: the startup wiring
// main.cpp does, which the hand-built pages in tst_page never exercise. This
// is where "Main.qml has no Page" regressed once.
#include <QtGui/QMouseEvent>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtTest>

#include "palette.h"
#include "page.h"
#include "pageitem.h"
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

void mouse(QQuickWindow &window, QEvent::Type type, const QPointF &pos,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent event(type, pos, window.mapToGlobal(pos),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      mods);
    QGuiApplication::sendEvent(&window, &event);
}

void drag(QQuickWindow &window, const QPointF &from, const QPointF &to)
{
    mouse(window, QEvent::MouseButtonPress, from);
    mouse(window, QEvent::MouseMove, to);
    mouse(window, QEvent::MouseButtonRelease, to);
}

} // namespace

class AppTest : public QObject
{
    Q_OBJECT

private slots:
    void mainQmlWiresThePageAndDraws();
    void selectFlowThroughMainQml();
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
    QCOMPARE(page->items().size(), 1);
    QCOMPARE(pixel(*window, QPointF(200, 300)), palette::ink);

    // The Ctrl shortcuts reach the page through the same wiring.
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(page->items().size(), 0);
    QCOMPARE(pixel(*window, QPointF(200, 300)), palette::page);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(page->items().size(), 1);
}

void AppTest::selectFlowThroughMainQml()
{
    // The select tool end to end: keys, pointer drags, undo — all through the
    // real Main.qml and the wiring main.cpp does.
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
    QVERIFY(page);
    tools.setPage(page);

    QTest::qWait(50);
    QTest::keyClick(window, Qt::Key_D);
    draw(*window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    draw(*window, {{100, 400}, {150, 400}, {200, 400}, {250, 400}, {300, 400}});

    // Ctrl+A selects everything and switches to the select tool.
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(tools.tool(), Tools::Select);
    QCOMPARE(page->selection().size(), 2);

    // Drag one selected stroke: the whole selection moves with the pointer.
    drag(*window, QPointF(150, 300), QPointF(190, 340));
    for (PageItem *item : page->selection())
        QCOMPARE(item->position(), QPointF(40, 40));
    QCOMPARE(pixel(*window, QPointF(190, 340)), palette::ink);

    // Delete, then undo brings the drawing back.
    QTest::keyClick(window, Qt::Key_Delete);
    QCOMPARE(page->items().size(), 0);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(page->items().size(), 2);
    QCOMPARE(pixel(*window, QPointF(190, 340)), palette::ink);
}

QTEST_MAIN(AppTest)
#include "tst_app.moc"
