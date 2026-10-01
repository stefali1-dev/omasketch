#include <QtGui/QMouseEvent>
#include <QtGui/QNativeGestureEvent>
#include <QtGui/QPointingDevice>
#include <QtGui/QWheelEvent>
#include <QtQuick/QQuickWindow>
#include <QtTest>

#include "palette.h"
#include "page.h"
#include "pageitem.h"
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

// Mouse events with modifiers (QTest::mouseMove cannot carry them). Sent
// directly to the window; QQuickWindow delivers them to the items.
void mouse(QQuickWindow &window, QEvent::Type type, const QPointF &pos,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent event(type, pos, window.mapToGlobal(pos),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      mods);
    QGuiApplication::sendEvent(&window, &event);
}

void click(QQuickWindow &window, const QPointF &pos,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(window, QEvent::MouseButtonPress, pos, mods);
    mouse(window, QEvent::MouseButtonRelease, pos, mods);
}

void drag(QQuickWindow &window, const QPointF &from, const QPointF &to,
          Qt::KeyboardModifiers moveMods = Qt::NoModifier)
{
    mouse(window, QEvent::MouseButtonPress, from);
    mouse(window, QEvent::MouseMove, to, moveMods);
    mouse(window, QEvent::MouseButtonRelease, to);
}

// How many pixels of the stroke's colour a full column at x crosses.
int inkRows(QQuickWindow &window, int x)
{
    const QImage image = window.grabWindow();
    const qreal dpr = image.devicePixelRatio();
    int rows = 0;
    for (int y = 0; y < image.height(); ++y) {
        if (image.pixelColor(qRound(x * dpr), y) == palette::ink)
            ++rows;
    }
    return rows;
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
    void spacePanKillsTheZoomEase();
    void quitMidStrokeFreesTheStroke();
    void wheelZoomStaysAnchoredAtTheCursor();
    void keyboardZoomStaysAnchoredAndClamps();
    void pinchZoomsSmoothly();
    void ctrlZeroBringsTheDrawingBack();
    void panAndZoomRebuildNoGeometry();
    void clickSelectsOnlyNearTheLine();
    void clickPicksTheTopmostStroke();
    void shiftClickAddsAndRemoves();
    void boxSelectTouchesTheInkNotTheBox();
    void boxSelectSelectsEverythingItTouchesAndClickClears();
    void dragMovesTheWholeSelectionExactly();
    void resizeKeepsTheOppositeCornerAndProportions();
    void shiftResizeIsFree();
    void resizeHandleShowsTheDiagonalCursor();
    void resizeKeepsTheLineWidth();
    void deleteRemovesAndUndoes();
    void recolourAppliesToTheSelectionAndUndoes();
    void eraseDragRemovesWithUndo();
    void ctrlASelectsAllAndSwitchesToSelect();
    void escapeClearsTheSelectionFirst();
    void switchingToolsClearsTheSelection();
};

void PageTest::drawShowsStrokeAndUndoRedoRemovesRestoresIt()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);

    const QPointF mid(200, 300);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});

    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(pixel(rig.window, mid), palette::ink);
    QVERIFY(static_cast<Stroke *>(rig.page->items().constFirst())
                ->geometryBuildCount() > 0);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0);
    QCOMPARE(pixel(rig.window, mid), palette::page);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 1);
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
    QCOMPARE(rig.page->items().size(), 0);
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::page);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::ink);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 0);
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

    // ...and a horizontal swipe pans on its own: scroll right moves the view
    // right. (Scroll right arrives as negative x: QtWayland negates the
    // Wayland "+x is right" value, KDE bug 417604, so adding the delta is the
    // fix — verified against a real window, not the doc sign.)
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

void PageTest::spacePanKillsTheZoomEase()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);

    rig.page->zoomStep(1); // starts the 120 ms ease
    QTest::keyPress(&rig.window, Qt::Key_Space);
    QTest::mousePress(&rig.window, Qt::LeftButton, {}, QPoint(300, 200));
    const QPointF start = rig.page->worldPos();

    QTest::mouseMove(&rig.window, QPoint(340, 260));
    QTest::qWait(60);
    QTest::mouseMove(&rig.window, QPoint(400, 300));
    QTest::qWait(60); // an ease left running would fight the drag here

    QCOMPARE(rig.page->worldPos(), start + QPointF(100, 100));

    QTest::mouseRelease(&rig.window, Qt::LeftButton, {}, QPoint(400, 300));
    QTest::keyRelease(&rig.window, Qt::Key_Space);
}

void PageTest::quitMidStrokeFreesTheStroke()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);

    QTest::mousePress(&rig.window, Qt::LeftButton, {}, QPoint(150, 150));
    QTest::mouseMove(&rig.window, QPoint(200, 160));
    QCOMPARE(rig.page->items().size(), 0); // in progress, not on the stack

    // The in-progress stroke has no QObject owner; find it under the world.
    QQuickItem *world = rig.page->childItems().constFirst();
    QCOMPARE(world->childItems().size(), 1);
    bool freed = false;
    QObject::connect(world->childItems().constFirst(), &QObject::destroyed, this,
                     [&freed] { freed = true; });

    delete rig.page; // quitting mid-stroke must not leak it
    rig.page = nullptr;
    QVERIFY(freed);
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
    auto *stroke = static_cast<Stroke *>(rig.page->items().constFirst());
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

void PageTest::clickSelectsOnlyNearTheLine()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{200, 150}, {200, 200}, {200, 250}, {200, 300}, {200, 350}});
    QTest::keyClick(&rig.window, Qt::Key_V);

    // 3 px from the line: a hit. The stroke width is only ~3 px, so this is
    // the "within a few screen pixels" tolerance doing its job.
    click(rig.window, QPointF(203, 250));
    QCOMPARE(rig.page->selection().size(), 1);
    QCOMPARE(rig.page->selection().constFirst(), rig.page->items().constFirst());

    // 12 px from the line, on empty page: nothing, and the click clears.
    click(rig.window, QPointF(212, 250));
    QCOMPARE(rig.page->selection().size(), 0);
}

void PageTest::clickPicksTheTopmostStroke()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    draw(rig.window, {{200, 150}, {200, 200}, {200, 250}, {200, 300}, {200, 350}});
    QTest::keyClick(&rig.window, Qt::Key_V);

    // Both strokes cross here; the later one renders on top and wins.
    click(rig.window, QPointF(200, 250));
    QCOMPARE(rig.page->selection().size(), 1);
    QCOMPARE(rig.page->selection().constFirst(), rig.page->items().constLast());

    click(rig.window, QPointF(120, 250)); // only the horizontal stroke
    QCOMPARE(rig.page->selection().size(), 1);
    QCOMPARE(rig.page->selection().constFirst(), rig.page->items().constFirst());
}

void PageTest::shiftClickAddsAndRemoves()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 200}, {150, 200}, {200, 200}, {250, 200}, {300, 200}});
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    QTest::keyClick(&rig.window, Qt::Key_V);

    click(rig.window, QPointF(150, 200));
    QCOMPARE(rig.page->selection().size(), 1);
    click(rig.window, QPointF(150, 300), Qt::ShiftModifier);
    QCOMPARE(rig.page->selection().size(), 2);
    click(rig.window, QPointF(150, 300), Qt::ShiftModifier); // toggled off again
    QCOMPARE(rig.page->selection().size(), 1);
}

void PageTest::boxSelectTouchesTheInkNotTheBox()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 100}, {150, 150}, {200, 200}, {250, 250}, {300, 300}});
    QTest::keyClick(&rig.window, Qt::Key_V);

    // Inside the diagonal's bounding box, well away from the line: untouched.
    drag(rig.window, QPointF(140, 180), QPointF(170, 210));
    QCOMPARE(rig.page->selection().size(), 0);

    // A box the line passes through: selected.
    drag(rig.window, QPointF(180, 120), QPointF(260, 200));
    QCOMPARE(rig.page->selection().size(), 1);
}

void PageTest::boxSelectSelectsEverythingItTouchesAndClickClears()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 200}, {150, 200}, {200, 200}, {250, 200}, {300, 200}});
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    QTest::keyClick(&rig.window, Qt::Key_V);

    drag(rig.window, QPointF(50, 150), QPointF(350, 350));
    QCOMPARE(rig.page->selection().size(), 2);

    click(rig.window, QPointF(500, 400)); // empty page
    QCOMPARE(rig.page->selection().size(), 0);
}

void PageTest::dragMovesTheWholeSelectionExactly()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 200}, {150, 200}, {200, 200}, {250, 200}, {300, 200}});
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    drag(rig.window, QPointF(50, 150), QPointF(350, 350));
    QCOMPARE(rig.page->selection().size(), 2);

    // Press on one selected stroke and drag: the whole selection follows the
    // pointer exactly (no easing to wait out).
    drag(rig.window, QPointF(150, 200), QPointF(190, 230));
    for (PageItem *item : rig.page->selection())
        QCOMPARE(item->position(), QPointF(40, 30));
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::page);
    QCOMPARE(pixel(rig.window, QPointF(190, 230)), palette::ink);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    for (PageItem *item : rig.page->selection())
        QCOMPARE(item->position(), QPointF(0, 0));
    QCOMPARE(rig.page->selection().size(), 2); // selection is not an undo step
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::ink);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->selection().constFirst()->position(), QPointF(40, 30));
}

void PageTest::resizeKeepsTheOppositeCornerAndProportions()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    // A diagonal stroke: extent on both axes, so the selection box has real
    // proportions to keep (a flat line's box is all width margin).
    draw(rig.window, {{200, 200}, {230, 230}, {260, 260}, {290, 290}, {320, 320}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(260, 260));
    const QRectF before = rig.page->selectionRect();
    QVERIFY(before.isValid());

    // Drag the bottom-right handle: the top-left corner stays put and the
    // proportions hold.
    drag(rig.window, before.bottomRight(), before.bottomRight() + QPointF(60, 60));
    const QRectF after = rig.page->selectionRect();
    // The corner is anchored for the ink, but the selection box includes the
    // line-width margin, which does not scale; it can shift by (s-1)*width.
    QVERIFY(qAbs(after.topLeft().x() - before.topLeft().x()) < 3);
    QVERIFY(qAbs(after.topLeft().y() - before.topLeft().y()) < 3);
    QVERIFY(after.width() > before.width());
    QVERIFY(qAbs(after.width() / after.height() - before.width() / before.height())
            < 0.01);

    // The pixels moved with the resize: the old spot is empty.
    QCOMPARE(pixel(rig.window, QPointF(260, 250)), palette::page);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(qAbs(rig.page->selectionRect().topLeft().x() - before.topLeft().x()) < 3);
    QVERIFY(qAbs(rig.page->selectionRect().topLeft().y() - before.topLeft().y()) < 3);
    QCOMPARE(rig.page->selectionRect().width(), before.width());
}

void PageTest::shiftResizeIsFree()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{200, 250}, {240, 250}, {280, 250}, {320, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(260, 250));
    const QRectF before = rig.page->selectionRect();

    mouse(rig.window, QEvent::MouseButtonPress, before.bottomRight());
    mouse(rig.window, QEvent::MouseMove, before.bottomRight() + QPointF(60, 0),
          Qt::ShiftModifier); // horizontal only: free resize stretches
    mouse(rig.window, QEvent::MouseButtonRelease,
          before.bottomRight() + QPointF(60, 0), Qt::ShiftModifier);

    const QRectF after = rig.page->selectionRect();
    QVERIFY(after.width() > before.width());
    QCOMPARE(after.height(), before.height()); // y did not move: free, not proportional
}

void PageTest::resizeHandleShowsTheDiagonalCursor()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    // A diagonal stroke: its corner handles are far apart, unlike a flat
    // line's, whose top and bottom handles share an edge.
    draw(rig.window, {{200, 200}, {230, 230}, {260, 260}, {290, 290}, {320, 320}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(260, 260));
    const QRectF rect = rig.page->selectionRect();

    const auto hover = [this, &rig](const QPointF &pos) {
        QHoverEvent event(QEvent::HoverMove, pos, rig.window.mapToGlobal(pos), QPointF());
        QGuiApplication::sendEvent(&rig.window, &event);
    };
    hover(rect.bottomRight());
    QCOMPARE(rig.window.cursor().shape(), Qt::SizeFDiagCursor);
    hover(rect.topLeft());
    QCOMPARE(rig.window.cursor().shape(), Qt::SizeFDiagCursor);
    hover(rect.topRight());
    QCOMPARE(rig.window.cursor().shape(), Qt::SizeBDiagCursor);
    hover(QPointF(100, 100)); // off the handles
    QCOMPARE(rig.window.cursor().shape(), Qt::ArrowCursor);
}

void PageTest::resizeKeepsTheLineWidth()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{200, 250}, {240, 250}, {280, 250}, {320, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(260, 250));

    // A full column: the line moves down as it grows, so scan it all. The
    // rasterized row count of a 2.75 px line can differ by one with the
    // sub-pixel phase; a proportionally stretched line would be ~1.5x more.
    const int before = inkRows(rig.window, 260);
    drag(rig.window, rig.page->selectionRect().bottomRight(),
         rig.page->selectionRect().bottomRight() + QPointF(60, 60));
    const int after = inkRows(rig.window, 260);
    QVERIFY(before >= 2);
    QVERIFY(qAbs(after - before) <= 1);
}

void PageTest::deleteRemovesAndUndoes()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(200, 250));

    QTest::keyClick(&rig.window, Qt::Key_Delete);
    QCOMPARE(rig.page->items().size(), 0);
    QCOMPARE(rig.page->selection().size(), 0);
    QCOMPARE(pixel(rig.window, QPointF(200, 250)), palette::page);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(pixel(rig.window, QPointF(200, 250)), palette::ink);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 0);
}

void PageTest::recolourAppliesToTheSelectionAndUndoes()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 200}, {150, 200}, {200, 200}, {250, 200}, {300, 200}});
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(150, 200));

    QTest::keyClick(&rig.window, Qt::Key_2); // red
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::red);
    QCOMPARE(pixel(rig.window, QPointF(150, 300)), palette::ink); // unselected

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::ink);

    // The same key set the ink: the next stroke comes out red.
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 400}, {150, 400}, {200, 400}, {250, 400}, {300, 400}});
    QCOMPARE(pixel(rig.window, QPointF(200, 400)), palette::red);
}

void PageTest::eraseDragRemovesWithUndo()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 200}, {150, 200}, {200, 200}, {250, 200}, {300, 200}});
    draw(rig.window, {{100, 400}, {150, 400}, {200, 400}, {250, 400}, {300, 400}});
    QTest::keyClick(&rig.window, Qt::Key_E);

    drag(rig.window, QPointF(150, 200), QPointF(250, 200));
    QCOMPARE(rig.page->items().size(), 1);
    QTest::qWait(150); // the erased stroke fades out for 80 ms first
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::page);
    QCOMPARE(pixel(rig.window, QPointF(150, 400)), palette::ink); // untouched

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 2);
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::ink);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 1);
}

void PageTest::ctrlASelectsAllAndSwitchesToSelect()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 150}, {150, 150}, {200, 150}});
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}});
    draw(rig.window, {{100, 350}, {150, 350}, {200, 350}});
    QCOMPARE(rig.tools.tool(), Tools::Draw);

    QTest::keyClick(&rig.window, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(rig.tools.tool(), Tools::Select);
    QCOMPARE(rig.page->selection().size(), 3);
}

void PageTest::escapeClearsTheSelectionFirst()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(200, 250));
    QCOMPARE(rig.page->selection().size(), 1);

    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.page->selection().size(), 0);
    QCOMPARE(rig.tools.tool(), Tools::Select); // still the select tool

    QTest::keyClick(&rig.window, Qt::Key_D);
    QTest::keyClick(&rig.window, Qt::Key_Escape); // second job of Esc
    QCOMPARE(rig.tools.tool(), Tools::Select);
}

void PageTest::switchingToolsClearsTheSelection()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(200, 250));
    QCOMPARE(rig.page->selection().size(), 1);

    QTest::keyClick(&rig.window, Qt::Key_D);
    QCOMPARE(rig.page->selection().size(), 0);
    QTest::keyClick(&rig.window, Qt::Key_V);
    QCOMPARE(rig.page->selection().size(), 0);
}

QTEST_MAIN(PageTest)
#include "tst_page.moc"
