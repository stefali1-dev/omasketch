#include <QtGui/QNativeGestureEvent>
#include <QtGui/QPointingDevice>
#include <QtGui/QWheelEvent>
#include <QtQuick/QQuickWindow>
#include <QtTest>

#include "palette.h"
#include "page.h"
#include "stroke.h"
#include "tools.h"

namespace {

// Window + page + tools, wired the way main.cpp wires them.
struct Rig
{
    QQuickWindow window;
    Tools tools;
    Page *page = nullptr;

    explicit Rig(QSizeF size = QSizeF(640, 480))
    {
        window.resize(size.toSize());
        window.setColor(palette::page);
        page = new Page(window.contentItem());
        page->setSize(size);
        QObject::connect(&window, &QQuickWindow::widthChanged, page,
                         [this] { page->setWidth(window.width()); });
        QObject::connect(&window, &QQuickWindow::heightChanged, page,
                         [this] { page->setHeight(window.height()); });
        tools.attach(&window);
        tools.setPage(page);
    }
};

void draw(QQuickWindow &window, const QList<QPoint> &points)
{
    QTest::mouseMove(&window, points.first());
    QTest::mousePress(&window, Qt::LeftButton, {}, points.first());
    for (int i = 1; i < points.size(); ++i)
        QTest::mouseMove(&window, points[i]);
    QTest::mouseRelease(&window, Qt::LeftButton, {}, points.last());
}

void wheel(QQuickWindow &window, QPointF pos, QPoint angleDelta,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWheelEvent event(pos, pos, QPoint(), angleDelta, Qt::NoButton, mods,
                      Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(&window, &event);
}

void pinch(QQuickWindow &window, QPointF pos, qreal value)
{
    QNativeGestureEvent event(Qt::ZoomNativeGesture,
                              QPointingDevice::primaryPointingDevice(), 2,
                              pos, pos, pos, value, QPointF(value, 0));
    QGuiApplication::sendEvent(&window, &event);
}

QColor pixel(QQuickWindow &window, const QPointF &pos)
{
    const QImage image = window.grabWindow();
    const qreal dpr = image.devicePixelRatio();
    return image.pixelColor(qRound(pos.x() * dpr), qRound(pos.y() * dpr));
}

QPointF worldUnder(Page *page, const QPointF &pagePos)
{
    return (pagePos - page->worldPos()) / page->zoom();
}

} // namespace

class PageTest : public QObject
{
    Q_OBJECT

private slots:
    void drawShowsStrokeAndUndoRedoRemovesRestoresIt();
    void tapLeavesADot();
    void freshPageIsUndoable();
    void wheelPansIncludingShiftAndHorizontal();
    void spacePansWithHandCursor();
    void wheelZoomStaysAnchoredAtTheCursor();
    void keyboardZoomStaysAnchoredAndClamps();
    void pinchZoomsSmoothly();
    void ctrlZeroBringsTheDrawingBack();
    void panAndZoomRebuildNoGeometry();
};

void PageTest::drawShowsStrokeAndUndoRedoRemovesRestoresIt()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);

    const QPointF mid(200, 300);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});

    QCOMPARE(rig.page->strokes().size(), 1);
    QCOMPARE(pixel(rig.window, mid), palette::ink);
    QVERIFY(rig.page->strokes().constFirst()->geometryBuildCount() > 0);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->strokes().size(), 0);
    QCOMPARE(pixel(rig.window, mid), palette::page);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->strokes().size(), 1);
    QCOMPARE(pixel(rig.window, mid), palette::ink);
}

void PageTest::tapLeavesADot()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);

    QTest::mousePress(&rig.window, Qt::LeftButton, {}, QPoint(150, 150));
    QTest::mouseRelease(&rig.window, Qt::LeftButton, {}, QPoint(150, 150));

    QCOMPARE(pixel(rig.window, QPointF(150, 150)), palette::ink);
}

void PageTest::freshPageIsUndoable()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {300, 300}});

    QTest::keyClick(&rig.window, Qt::Key_N, Qt::ControlModifier);
    QCOMPARE(rig.page->strokes().size(), 0);
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::page);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->strokes().size(), 1);
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::ink);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->strokes().size(), 0);
}

void PageTest::wheelPansIncludingShiftAndHorizontal()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QCOMPARE(rig.page->worldPos(), QPointF(0, 0));

    wheel(rig.window, QPointF(320, 240), QPoint(0, 120));
    QCOMPARE(rig.page->worldPos(), QPointF(0, 120));

    // Shift turns a vertical wheel into a horizontal pan...
    wheel(rig.window, QPointF(320, 240), QPoint(0, 120), Qt::ShiftModifier);
    QCOMPARE(rig.page->worldPos(), QPointF(120, 120));

    // ...a horizontal swipe pans horizontally on its own.
    wheel(rig.window, QPointF(320, 240), QPoint(120, 0));
    QCOMPARE(rig.page->worldPos(), QPointF(240, 120));

    // Touchpads send pixel deltas instead.
    QWheelEvent touch(QPointF(320, 240), QPointF(320, 240), QPoint(0, 10),
                      QPoint(), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(&rig.window, &touch);
    QCOMPARE(rig.page->worldPos(), QPointF(240, 130));
}

void PageTest::spacePansWithHandCursor()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QCOMPARE(rig.window.cursor().shape(), Qt::ArrowCursor);

    QTest::keyPress(&rig.window, Qt::Key_Space);
    QCOMPARE(rig.window.cursor().shape(), Qt::OpenHandCursor);

    QTest::mousePress(&rig.window, Qt::LeftButton, {}, QPoint(300, 200));
    QCOMPARE(rig.window.cursor().shape(), Qt::ClosedHandCursor);
    const QPointF start = rig.page->worldPos();

    QTest::mouseMove(&rig.window, QPoint(340, 260));
    QCOMPARE(rig.page->worldPos(), start + QPointF(40, 60));

    QTest::mouseRelease(&rig.window, Qt::LeftButton, {}, QPoint(340, 260));
    QCOMPARE(rig.page->worldPos(), start + QPointF(40, 60));
    QCOMPARE(rig.window.cursor().shape(), Qt::OpenHandCursor);

    QTest::keyRelease(&rig.window, Qt::Key_Space);
    QCOMPARE(rig.window.cursor().shape(), Qt::ArrowCursor);
}

void PageTest::wheelZoomStaysAnchoredAtTheCursor()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {300, 300}});

    const QPointF anchor(200, 300); // on the stroke
    QTest::mouseMove(&rig.window, anchor.toPoint());
    const QPointF before = worldUnder(rig.page, anchor);

    wheel(rig.window, anchor, QPoint(0, 120), Qt::ControlModifier);
    QCOMPARE(rig.page->zoom(), 1.25);
    wheel(rig.window, anchor, QPoint(0, 120), Qt::ControlModifier);
    QCOMPARE(rig.page->zoom(), 1.5625);
    QCOMPARE((anchor - rig.page->worldPos()) / rig.page->zoom(), before);

    // The anchored stroke point still lands on the cursor on screen.
    QCOMPARE(pixel(rig.window, anchor), palette::ink);

    wheel(rig.window, anchor, QPoint(0, -120), Qt::ControlModifier);
    wheel(rig.window, anchor, QPoint(0, -120), Qt::ControlModifier);
    QCOMPARE(rig.page->zoom(), 1.0);
}

void PageTest::keyboardZoomStaysAnchoredAndClamps()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {300, 300}});

    const QPointF anchor(500, 400);
    QTest::mouseMove(&rig.window, anchor.toPoint());
    const QPointF before = worldUnder(rig.page, anchor);

    QTest::keyClick(&rig.window, Qt::Key_Equal, Qt::ControlModifier);
    QTest::qWait(300);
    QCOMPARE(rig.page->zoom(), 1.25);
    QCOMPARE((anchor - rig.page->worldPos()) / rig.page->zoom(), before);

    QTest::keyClick(&rig.window, Qt::Key_Minus, Qt::ControlModifier);
    QTest::qWait(300);
    QCOMPARE(rig.page->zoom(), 1.0);

    for (int i = 0; i < 40; ++i)
        wheel(rig.window, anchor, QPoint(0, 120), Qt::ControlModifier);
    QCOMPARE(rig.page->zoom(), 8.0);
    for (int i = 0; i < 80; ++i)
        wheel(rig.window, anchor, QPoint(0, -120), Qt::ControlModifier);
    QCOMPARE(rig.page->zoom(), 0.1);
}

void PageTest::pinchZoomsSmoothly()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {300, 300}});

    const QPointF anchor(200, 300);
    const QPointF before = worldUnder(rig.page, anchor);

    for (int i = 0; i < 20; ++i)
        pinch(rig.window, anchor, 0.05);
    QVERIFY(rig.page->zoom() > 1.5);
    QCOMPARE((anchor - rig.page->worldPos()) / rig.page->zoom(), before);
    QCOMPARE(pixel(rig.window, anchor), palette::ink);
}

void PageTest::ctrlZeroBringsTheDrawingBack()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {300, 300}});

    const QPointF anchor(320, 240);
    for (int i = 0; i < 4; ++i)
        wheel(rig.window, anchor, QPoint(0, 120), Qt::ControlModifier);
    QVERIFY(rig.page->zoom() > 2.0);

    QTest::keyClick(&rig.window, Qt::Key_0, Qt::ControlModifier);
    QTest::qWait(400);
    QCOMPARE(rig.page->zoom(), 1.0);
    QCOMPARE(rig.page->worldPos(),
             QPointF(rig.page->width() / 2, rig.page->height() / 2)
                 - rig.page->drawingBounds().center());

    // The stroke is centred and its pixels are back where the math says.
    const QPointF strokeMid = rig.page->worldPos() + QPointF(200, 300);
    QCOMPARE(pixel(rig.window, strokeMid), palette::ink);
}

void PageTest::panAndZoomRebuildNoGeometry()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {300, 300}});
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::ink); // forces a render
    Stroke *stroke = rig.page->strokes().constFirst();
    const int builds = stroke->geometryBuildCount();
    QVERIFY(builds > 0);

    for (int i = 0; i < 5; ++i) {
        wheel(rig.window, QPointF(320, 240), QPoint(0, 120));
        wheel(rig.window, QPointF(320, 240), QPoint(0, 100), Qt::ControlModifier);
    }
    QTest::keyPress(&rig.window, Qt::Key_Space);
    QTest::mousePress(&rig.window, Qt::LeftButton, {}, QPoint(300, 200));
    QTest::mouseMove(&rig.window, QPoint(200, 100));
    QTest::mouseRelease(&rig.window, Qt::LeftButton, {}, QPoint(200, 100));
    QTest::keyRelease(&rig.window, Qt::Key_Space);

    QCOMPARE(stroke->geometryBuildCount(), builds);
}

QTEST_MAIN(PageTest)
#include "tst_page.moc"
