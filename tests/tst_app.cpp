// Loads the real Main.qml module and draws through it: the startup wiring
// main.cpp does, which the hand-built pages in tst_page never exercise. This
// is where "Main.qml has no Page" regressed once.
#include <QtGui/QMouseEvent>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "files.h"
#include "palette.h"
#include "page.h"
#include "pageitem.h"
#include "textbox.h"
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

void type(QQuickWindow &window, const QString &text)
{
    for (const QChar c : text)
        QTest::keyClick(&window, c.toLatin1());
}

// Whether the accent appears in the rect: the caret of a box being edited.
// The caret blinks, so the caller may need a few grabs.
bool accentIn(QQuickWindow &window, const QRectF &rect)
{
    const QImage image = window.grabWindow();
    const qreal dpr = image.devicePixelRatio();
    for (int y = qRound(rect.top() * dpr); y < qRound(rect.bottom() * dpr); ++y) {
        for (int x = qRound(rect.left() * dpr); x < qRound(rect.right() * dpr); ++x) {
            if (image.pixelColor(x, y) == palette::accent)
                return true;
        }
    }
    return false;
}

} // namespace

class AppTest : public QObject
{
    Q_OBJECT

private slots:
    void mainQmlWiresThePageAndDraws();
    void selectFlowThroughMainQml();
    void textFlowThroughMainQml();
    void saveFlowThroughMainQml();
    void closeAutosavesThroughMainQml();
    void arrowFlowThroughMainQml();
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

void AppTest::textFlowThroughMainQml()
{
    // The text tool end to end through the real Main.qml: tool key, click,
    // typing with the accent caret, commit, undo — and Esc back to select.
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
    QTest::keyClick(window, Qt::Key_T);
    QCOMPARE(tools.tool(), Tools::Text);
    QCOMPARE(window->cursor().shape(), Qt::IBeamCursor);

    mouse(*window, QEvent::MouseButtonPress, QPointF(300, 300));
    mouse(*window, QEvent::MouseButtonRelease, QPointF(300, 300));
    QVERIFY(page->editing());

    type(*window, QStringLiteral("if (x == 0)"));
    QTest::keyClick(window, Qt::Key_Return);
    type(*window, QStringLiteral("    return [1, 3, 5];"));

    // The accent caret shows in the box (it blinks: try across a period).
    bool caret = false;
    for (int i = 0; i < 12 && !caret; ++i) {
        caret = accentIn(*window, QRectF(298, 298, 260, 50));
        if (!caret)
            QTest::qWait(80);
    }
    QVERIFY(caret);

    mouse(*window, QEvent::MouseButtonPress, QPointF(650, 400));
    mouse(*window, QEvent::MouseButtonRelease, QPointF(650, 400)); // commit + new box
    QCOMPARE(page->items().size(), 1);
    QVERIFY(page->editing());
    QTest::keyClick(window, Qt::Key_Escape); // the empty new box just goes

    QCOMPARE(page->items().size(), 1);
    auto *box = qobject_cast<TextBox *>(page->items().constFirst());
    QVERIFY(box);
    QCOMPARE(box->text(), QStringLiteral("if (x == 0)\n    return [1, 3, 5];"));

    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(page->items().size(), 0);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(page->items().size(), 1);

    QTest::keyClick(window, Qt::Key_Escape);
    QCOMPARE(tools.tool(), Tools::Select);
}

void AppTest::saveFlowThroughMainQml()
{
    // Ctrl+S through the real Main.qml: the shortcut reaches Files, the
    // drawing lands in HOME/Pictures/Drawings and the toast is asked for.
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("HOME", home.path().toUtf8());

    Tools tools;
    Files files;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Colors", paletteMap());
    engine.rootContext()->setContextProperty("tools", &tools);
    engine.rootContext()->setContextProperty("files", &files);
    engine.loadFromModule("Omasketch", "Main");
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    tools.attach(window);
    Page *page = pageIn(window);
    QVERIFY(page);
    tools.setPage(page);
    files.setPage(page);
    QObject::connect(&tools, &Tools::saveRequested, &files, &Files::save);

    // Wait for the first rendered frame, so the render thread is settled
    // before the save renders offscreen.
    bool frameDone = false;
    QObject::connect(window, &QQuickWindow::frameSwapped, window,
                     [&] { frameDone = true; }, Qt::SingleShotConnection);
    QElapsedTimer waited;
    waited.start();
    while (!frameDone && waited.elapsed() < 2000)
        QTest::qWait(10);
    QTest::keyClick(window, Qt::Key_D);
    draw(*window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});

    QSignalSpy toast(&files, &Files::toastRequested);
    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);

    const QStringList saved = QDir(home.filePath("Pictures/Drawings"))
                                  .entryList(QDir::Files);
    QCOMPARE(saved.size(), 1);
    QCOMPARE(toast.size(), 1);
    QVERIFY(toast.constFirst().constFirst().toString().startsWith("saved → ~/"));
}

void AppTest::closeAutosavesThroughMainQml()
{
    // A window close (Super+W / Super+Q on Hyprland) fires QQuickWindow's
    // closing signal, and the wiring main.cpp sets must quietly auto-save
    // the drawing into HOME/Pictures/Drawings.
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("HOME", home.path().toUtf8());

    Tools tools;
    Files files;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Colors", paletteMap());
    engine.rootContext()->setContextProperty("tools", &tools);
    engine.rootContext()->setContextProperty("files", &files);
    engine.loadFromModule("Omasketch", "Main");
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    tools.attach(window);
    Page *page = pageIn(window);
    QVERIFY(page);
    tools.setPage(page);
    files.setPage(page);
    QObject::connect(window, &QQuickWindow::closing, &files, &Files::appClosing);

    QTest::keyClick(window, Qt::Key_D);
    draw(*window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});

    QVERIFY(window->close());
    const QStringList saved = QDir(home.filePath("Pictures/Drawings"))
                                  .entryList(QDir::Files);
    QCOMPARE(saved.size(), 1);
}

void AppTest::arrowFlowThroughMainQml()
{
    // The arrow tool end to end through the real Main.qml: the key and the
    // crosshair, pixels at the shaft and the head, the tool staying arrow,
    // recolour and undo — all through the wiring main.cpp does.
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
    QTest::keyClick(window, Qt::Key_A);
    QCOMPARE(tools.tool(), Tools::Arrow);
    QCOMPARE(window->cursor().shape(), Qt::CrossCursor);

    draw(*window, {{100, 300}, {200, 300}, {300, 300}});
    QCOMPARE(page->items().size(), 1);
    QCOMPARE(pixel(*window, QPointF(200, 300)), palette::ink); // the shaft
    QCOMPARE(pixel(*window, QPointF(295, 300)), palette::ink); // the head

    draw(*window, {{100, 400}, {200, 400}, {300, 400}});
    QCOMPARE(page->items().size(), 2); // the tool stayed arrow

    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(tools.tool(), Tools::Select);
    QTest::keyClick(window, Qt::Key_2); // recolour both
    QCOMPARE(pixel(*window, QPointF(200, 300)), palette::red);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(pixel(*window, QPointF(200, 300)), palette::ink);

    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(page->items().size(), 1);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(page->items().size(), 0);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(page->items().size(), 1);
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(page->items().size(), 2);
}

QTEST_MAIN(AppTest)
#include "tst_app.moc"
