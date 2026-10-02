#include <QtGui/QFontInfo>
#include <QtGui/QFontMetricsF>
#include <QtGui/QMouseEvent>
#include <QtGui/QNativeGestureEvent>
#include <QtGui/QPointingDevice>
#include <QtGui/QWheelEvent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickWindow>
#include <QtTest>
#include <QtMath>

#include "arrow.h"
#include "imageitem.h"
#include "palette.h"
#include "page.h"
#include "pageitem.h"
#include "stroke.h"
#include "textbox.h"
#include "tools.h"

namespace {

// Window + page + tools, wired the way main.cpp wires them. The QML engine
// gives the text boxes a context: the editor's accent caret delegate is a
// QQmlComponent, which needs an engine even in a hand-built window.
struct Rig
{
    QQuickWindow window;
    Tools tools;
    QQmlEngine engine;
    Page *page = nullptr;

    explicit Rig(QSizeF size = QSizeF(640, 480))
    {
        window.resize(size.toSize());
        window.setColor(palette::page);
        page = new Page(window.contentItem());
        QQmlEngine::setContextForObject(
            page, new QQmlContext(engine.rootContext()));
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

// How many ink-coloured pixels the rect holds (text glyphs at a live drag
// scale keep pure-ink cores; the grey and accent overlays never match).
int inkIn(QQuickWindow &window, const QRectF &rect)
{
    const QImage image = window.grabWindow();
    const qreal dpr = image.devicePixelRatio();
    int n = 0;
    for (int y = qRound(rect.top() * dpr); y < qRound(rect.bottom() * dpr); ++y) {
        for (int x = qRound(rect.left() * dpr); x < qRound(rect.right() * dpr); ++x) {
            if (image.pixelColor(x, y) == palette::ink)
                ++n;
        }
    }
    return n;
}

// Whether anything non-page is drawn in the rect: text glyphs or a caret,
// either of which survives antialiasing with at least one off-page pixel.
bool anythingDrawn(QQuickWindow &window, const QRectF &rect)
{
    const QImage image = window.grabWindow();
    const qreal dpr = image.devicePixelRatio();
    for (int y = qRound(rect.top() * dpr); y < qRound(rect.bottom() * dpr); ++y) {
        for (int x = qRound(rect.left() * dpr); x < qRound(rect.right() * dpr); ++x) {
            if (image.pixelColor(x, y) != palette::page)
                return true;
        }
    }
    return false;
}

void type(QQuickWindow &window, const QString &text)
{
    for (const QChar c : text)
        QTest::keyClick(&window, c.toLatin1());
}

// Places a committed box with the given text through the real tool flow.
TextBox *placeBox(Rig &rig, const QPointF &pos, const QString &text)
{
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, pos);
    type(rig.window, text);
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    return static_cast<TextBox *>(rig.page->items().constLast());
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
    void aDrawingStrokeMirrorsTheWorldThenJoinsIt();
    void aFastStrokesPathIsSampledDensely();
    void clickSelectsOnlyNearTheLine();
    void clickPicksTheTopmostStroke();
    void shiftClickAddsAndRemoves();
    void boxSelectTouchesTheInkNotTheBox();
    void boxSelectSelectsEverythingItTouchesAndClickClears();
    void dragMovesTheWholeSelectionExactly();
    void resizeKeepsTheOppositeCornerAndProportions();
    void shiftResizeIsFree();
    void shiftResizePastTheAnchorNeverFlips();
    void shiftResizePastTheAnchorKeepsImagesPositive();
    void textBoxFollowsTheLiveResize();
    void resizeHandleShowsTheDiagonalCursor();
    void resizeKeepsTheLineWidth();
    void deleteRemovesAndUndoes();
    void recolourAppliesToTheSelectionAndUndoes();
    void eraseDragRemovesWithUndo();
    void ctrlASelectsAllAndSwitchesToSelect();
    void escapeClearsTheSelectionFirst();
    void switchingToolsClearsTheSelection();
    void selectionBoxKeepsAPadAroundTheInk();
    void handleGrabPicksTheNearestCorner();
    void pressInsideTheBoxMovesTheSelection();
    void recolourWorksWhenTheInkAlreadyMatches();
    void escapeCancelsADragThenClearsTheSelection();
    void undoOfDeleteRestoresTheStackingOrder();
    void undoOfEraseRestoresTheStackingOrder();
    void startingAMarqueeHidesTheOldBoxAtOnce();
    void zoomIsBlockedMidDrag();
    void wheelPanIsBlockedMidDrag();
    void textToolPlacesAndTypesAndStays();
    void toolKeysGoIntoTheText();
    void escapeTwiceReturnsToSelect();
    void emptyBoxRemovedWithoutAnUndoStep();
    void createUndoRedo();
    void clickOnABoxEditsAtTheClick();
    void editUndoRedo();
    void ctrlZUndoesInsideTheBoxWhileEditing();
    void enterAddsALine();
    void boxSelectsMovesResizesDeletes();
    void doubleClickEditsABox();
    void clickAwayCommitsAndPlacesANewBox();
    void typingIsFastWith200Boxes();
    void textBoxTypesInJetBrainsMono();
    void saveAndFreshPageCommitTheEdit();
    void warmTextEditorLeavesNoTrace();
    void quitWhileEditingAnExistingBoxFreesItOnce();
    void quitWhileEditingANewBoxFreesIt();
    void emptyingAnExistingBoxRemovesItUndoable();
    void boundsFollowTheLiveTextWhileEditing();
    void selectToolClickAwayCommitsTheEdit();
    void zoomKeysWorkWhileTyping();
    void arrowDrawsShaftAndHeadWithUndoRedo();
    void arrowShiftSnapsTo45Degrees();
    void arrowClickWithoutDragMakesNothing();
    void arrowHitTestsNearTheInkNotTheBox();
    void arrowMarqueeSelectsThroughTheHeadAlone();
    void arrowMinDragIsScreenPixels();
    void arrowMovesResizesDeletesLikeAStroke();
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

    // The in-progress stroke has no QObject owner; find it on the page
    // (it lives outside the world while drawing).
    QVERIFY(rig.page->childItems().contains(rig.page->drawing()));
    bool freed = false;
    QObject::connect(rig.page->drawing(), &QObject::destroyed, this,
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

// While drawing, the in-progress stroke lives on the page under a mirror
// of the world's transform (page.cpp: it keeps the per-frame geometry
// uploads out of the world's merged batches). Pin the mirror and the
// rendering: the ink still passes through the pointer at zoom 2, and the
// finished stroke joins the world at identity.
// A fast flick arrives as a few far-apart points; the smoothed path must
// still sample the curve finely (stroke.cpp grows the subdivision count
// with the segment length), or the straight chords stay visible.
void PageTest::aFastStrokesPathIsSampledDensely()
{
    Stroke stroke;
    stroke.setStrokeWidth(2.75);
    const QPointF centre(200, 300);
    stroke.begin(centre + QPointF(260, 0));
    for (int i = 1; i <= 6; ++i) {
        const qreal a = qDegreesToRadians(15.0 * i);
        stroke.addPoint(centre + QPointF(260 * std::cos(a), -260 * std::sin(a)));
    }
    const QList<QPointF> path = stroke.smoothedPath();
    // The spans raw-end -> first/last midpoint are curve tangents, not
    // chords; everything between must sample the curve at a few pixels.
    qreal maxChord = 0;
    for (int i = 2; i + 1 < path.size(); ++i)
        maxChord = qMax(maxChord, QLineF(path[i - 1], path[i]).length());
    QVERIFY(maxChord < 10.0);
}

void PageTest::aDrawingStrokeMirrorsTheWorldThenJoinsIt()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    pinch(rig.window, QPointF(320, 240), 1.0); // zoom 2
    QCOMPARE(rig.page->zoom(), 2.0);

    QTest::mouseMove(&rig.window, QPoint(300, 240));
    QTest::mousePress(&rig.window, Qt::LeftButton, {}, QPoint(300, 240));
    QTest::mouseMove(&rig.window, QPoint(340, 240));

    PageItem *drawing = rig.page->drawing();
    QVERIFY(drawing);
    QVERIFY(!rig.page->items().contains(drawing));
    QCOMPARE(drawing->parentItem(), rig.page);
    QCOMPARE(drawing->scale(), rig.page->zoom());
    QCOMPARE(drawing->position(), rig.page->worldPos());
    QCOMPARE(pixel(rig.window, QPointF(320, 240)), palette::ink); // forces a render

    QTest::mouseRelease(&rig.window, Qt::LeftButton, {}, QPoint(340, 240));
    QCOMPARE(drawing->parentItem(), rig.page->worldItem());
    QCOMPARE(drawing->scale(), 1.0);
    QCOMPARE(drawing->position(), QPointF(0, 0));
    QVERIFY(rig.page->items().contains(drawing));
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
    // line-width margin and the constant pad, which do not scale; the box
    // corner can shift by (margin + pad) * (s - 1), here ~4 px.
    QVERIFY(qAbs(after.topLeft().x() - before.topLeft().x()) < 5);
    QVERIFY(qAbs(after.topLeft().y() - before.topLeft().y()) < 5);
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

// A Shift (free) resize dragged past the anchor used to flip the items:
// text's sqrt of a negative product folded its font to 1 px, where not even
// undo brought it back, and images baked a negative width that renders
// nothing. Resizing never mirrors.
void PageTest::shiftResizePastTheAnchorNeverFlips()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    TextBox *box = placeBox(rig, QPointF(200, 200), QStringLiteral("abcd"));
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(210, 210));
    const qreal sizeBefore = box->fontSize();
    const QRectF rect = rig.page->selectionRect();

    // The bottom-right handle dragged past the left edge: sx < 0, sy == 1.
    mouse(rig.window, QEvent::MouseButtonPress, rect.bottomRight());
    mouse(rig.window, QEvent::MouseMove, QPointF(rect.left() - 30, rect.bottom()),
          Qt::ShiftModifier);
    mouse(rig.window, QEvent::MouseButtonRelease,
          QPointF(rect.left() - 30, rect.bottom()), Qt::ShiftModifier);

    QVERIFY(box->fontSize() > 1.0); // no NaN folded to the 1 px floor
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(qAbs(box->fontSize() - sizeBefore) < 0.01); // a big shrink undoes
}

void PageTest::shiftResizePastTheAnchorKeepsImagesPositive()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::red);
    auto *image = new ImageItem;
    image->setImage(source, QSizeF(64, 48));
    image->setPosition(QPointF(200, 200));
    rig.page->addItem(image);
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(230, 220));
    const QRectF rect = rig.page->selectionRect();

    mouse(rig.window, QEvent::MouseButtonPress, rect.bottomRight());
    mouse(rig.window, QEvent::MouseMove, QPointF(rect.left() - 30, rect.bottom()),
          Qt::ShiftModifier);
    mouse(rig.window, QEvent::MouseButtonRelease,
          QPointF(rect.left() - 30, rect.bottom()), Qt::ShiftModifier);
    QVERIFY(image->width() > 0); // a flip was never part of the deal

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(image->size(), QSizeF(64, 48));
    QCOMPARE(image->position(), QPointF(200, 200));
}

// Strokes, arrows and images scale live through the shared visual-scale
// transform while a resize drag runs; the text box's idle node used to stay
// at its old size until release.
void PageTest::textBoxFollowsTheLiveResize()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    TextBox *box = placeBox(rig, QPointF(200, 200), QStringLiteral("abcd"));
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(210, 210));
    const QRectF rect = rig.page->selectionRect();

    // A band right of the idle text: mid-drag only the grown glyphs are
    // there (the overlay draws in grey and accent, never ink).
    const QRectF band(rect.right() + 5, rect.top(), 45, rect.height());
    mouse(rig.window, QEvent::MouseButtonPress, rect.bottomRight());
    mouse(rig.window, QEvent::MouseMove, rect.bottomRight() + QPointF(60, 60));
    QVERIFY2(inkIn(rig.window, band) > 10, "text did not follow the drag");
    mouse(rig.window, QEvent::MouseButtonRelease,
          rect.bottomRight() + QPointF(60, 60));
    QVERIFY(box->fontSize() > 22); // the drag still bakes on release
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

void PageTest::selectionBoxKeepsAPadAroundTheInk()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 200}, {150, 200}, {200, 200}, {250, 200}, {300, 200}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(150, 200));

    // The outline floats a few screen pixels off the ink instead of sitting
    // on top of the line; the handles sit on these padded corners.
    QCOMPARE(rig.page->selectionRect(),
             rig.page->items().constFirst()->bounds().adjusted(-6, -6, 6, 6));
}

void PageTest::handleGrabPicksTheNearestCorner()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    // A flat line: its box is short enough that a press near the right edge
    // is within grab range of both right-hand handles. The nearer one must
    // win — grabbing the far one anchors the resize at the wrong corner and
    // the box ends up moving the other way.
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(150, 250));
    const QRectF before = rig.page->selectionRect();

    const QPointF press(before.right(), before.top() + 8.9); // nearer to BR
    drag(rig.window, press, press + QPointF(40, 40));
    const QRectF after = rig.page->selectionRect();
    QVERIFY(after.topLeft().y() > before.topLeft().y());
}

void PageTest::pressInsideTheBoxMovesTheSelection()
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

    // Press between the strokes, inside the box, off any ink: the whole
    // selection moves, like Figma and Excalidraw. Handles and Shift+click
    // keep their own behaviour (tested above).
    drag(rig.window, QPointF(200, 250), QPointF(240, 280));
    QCOMPARE(rig.page->selection().size(), 2);
    for (PageItem *item : rig.page->selection())
        QCOMPARE(item->position(), QPointF(40, 30));
}

void PageTest::recolourWorksWhenTheInkAlreadyMatches()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    QTest::keyClick(&rig.window, Qt::Key_2); // red ink
    draw(rig.window, {{100, 200}, {150, 200}, {200, 200}, {250, 200}, {300, 200}});
    QTest::keyClick(&rig.window, Qt::Key_1); // black ink again, nothing selected
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(150, 200)); // the red stroke
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::red);

    QTest::keyClick(&rig.window, Qt::Key_1); // the ink already matches
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::ink);

    // A second press changes nothing, so it must not mint an undo step:
    // the next undo hits the recolour, not a no-op.
    QTest::keyClick(&rig.window, Qt::Key_1);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(pixel(rig.window, QPointF(150, 200)), palette::red);
}

void PageTest::escapeCancelsADragThenClearsTheSelection()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(150, 250));

    // Esc while the move drag is held: the drag cancels (nothing commits),
    // then the selection clears.
    mouse(rig.window, QEvent::MouseButtonPress, QPointF(150, 250));
    mouse(rig.window, QEvent::MouseMove, QPointF(180, 250));
    QCOMPARE(rig.page->items().constFirst()->position(), QPointF(30, 0));
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.page->selection().size(), 0);
    QCOMPARE(rig.page->items().constFirst()->position(), QPointF(0, 0));

    // The release lands with no drag; undo reaches the draw, not a move.
    mouse(rig.window, QEvent::MouseButtonRelease, QPointF(180, 250));
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0);
}

void PageTest::undoOfDeleteRestoresTheStackingOrder()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    QTest::keyClick(&rig.window, Qt::Key_2); // red, drawn first: the bottom
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_1); // black, drawn last: the top
    draw(rig.window, {{200, 150}, {200, 200}, {200, 250}, {200, 300}, {200, 350}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    // Select in reverse stacking order, so the selection order differs from
    // the stacking: click takes the topmost, Shift+click adds the bottom one.
    click(rig.window, QPointF(200, 250));
    click(rig.window, QPointF(120, 250), Qt::ShiftModifier);
    QCOMPARE(rig.page->selection().size(), 2);
    PageItem *bottom = rig.page->items().constFirst();
    PageItem *top = rig.page->items().constLast();

    QTest::keyClick(&rig.window, Qt::Key_Delete);
    QCOMPARE(rig.page->items().size(), 0);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 2);
    QCOMPARE(rig.page->items().constFirst(), bottom);
    QCOMPARE(rig.page->items().constLast(), top);
}

void PageTest::undoOfEraseRestoresTheStackingOrder()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    QTest::keyClick(&rig.window, Qt::Key_2); // red, drawn first: the bottom
    draw(rig.window, {{100, 200}, {150, 200}, {200, 200}, {250, 200}, {300, 200}});
    QTest::keyClick(&rig.window, Qt::Key_1); // black, crossing it at (200, 200)
    draw(rig.window, {{150, 250}, {175, 225}, {200, 200}, {225, 175}, {250, 150}});
    PageItem *top = rig.page->items().constLast();
    QTest::keyClick(&rig.window, Qt::Key_E);

    // The eraser circle stays clear of the diagonal: only the bottom line goes.
    drag(rig.window, QPointF(110, 200), QPointF(140, 200));
    QCOMPARE(rig.page->items().size(), 1);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 2);
    QCOMPARE(rig.page->items().constLast(), top);
    QCOMPARE(pixel(rig.window, QPointF(200, 200)), palette::ink); // still on top
}

void PageTest::startingAMarqueeHidesTheOldBoxAtOnce()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(150, 250));
    const QRectF box = rig.page->selectionRect();
    QTest::qWait(120); // the box fades in for 80 ms before it is fully opaque
    QCOMPARE(pixel(rig.window, box.topLeft()), palette::accent); // handle shown

    // Pressing on empty page starts a marquee and clears the selection: the
    // old box must vanish right away, not linger until the first move.
    mouse(rig.window, QEvent::MouseButtonPress, QPointF(500, 400));
    QCOMPARE(pixel(rig.window, box.topLeft()), palette::page);
    mouse(rig.window, QEvent::MouseButtonRelease, QPointF(500, 400));
    QCOMPARE(rig.page->selection().size(), 0);
}

void PageTest::zoomIsBlockedMidDrag()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(150, 250));

    // Ctrl+wheel (also Super+scroll) and pinch would shift the world under
    // a held drag; they must not zoom until the drag is done.
    mouse(rig.window, QEvent::MouseButtonPress, QPointF(150, 250));
    mouse(rig.window, QEvent::MouseMove, QPointF(180, 250));
    wheel(rig.window, QPointF(320, 240), QPoint(0, 120), Qt::ControlModifier);
    QCOMPARE(rig.page->zoom(), 1.0);
    pinch(rig.window, QPointF(320, 240), 0.05);
    QCOMPARE(rig.page->zoom(), 1.0);
    mouse(rig.window, QEvent::MouseButtonRelease, QPointF(180, 250));
    QCOMPARE(rig.page->items().constFirst()->position(), QPointF(30, 0));
}

// A plain wheel or trackpad pan mid-drag used to carry the world (dragged
// items included) until the next pointer move, then snap it back.
void PageTest::wheelPanIsBlockedMidDrag()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(150, 250));

    mouse(rig.window, QEvent::MouseButtonPress, QPointF(150, 250));
    mouse(rig.window, QEvent::MouseMove, QPointF(180, 250));
    const QPointF world = rig.page->worldPos();
    wheel(rig.window, QPointF(320, 240), QPoint(0, 120)); // one notch
    QCOMPARE(rig.page->worldPos(), world);
    QWheelEvent touchpad(QPointF(320, 240), QPointF(320, 240), QPoint(0, 10),
                         QPoint(), Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(&rig.window, &touchpad);
    QCOMPARE(rig.page->worldPos(), world);
    mouse(rig.window, QEvent::MouseButtonRelease, QPointF(180, 250));
    QCOMPARE(rig.page->items().constFirst()->position(), QPointF(30, 0));
}

void PageTest::textToolPlacesAndTypesAndStays()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);

    QTest::keyClick(&rig.window, Qt::Key_T);
    QCOMPARE(rig.tools.tool(), Tools::Text);
    QCOMPARE(rig.window.cursor().shape(), Qt::IBeamCursor);

    click(rig.window, QPointF(200, 200));
    QVERIFY(rig.page->editing());
    QCOMPARE(rig.page->items().size(), 0); // committed only when the edit ends
    type(rig.window, "int n = 5");
    QTest::keyClick(&rig.window, Qt::Key_Escape);

    QCOMPARE(rig.tools.tool(), Tools::Text); // the tool stays text
    QCOMPARE(rig.page->items().size(), 1);
    auto *box = qobject_cast<TextBox *>(rig.page->items().constFirst());
    QVERIFY(box);
    QCOMPARE(box->text(), QStringLiteral("int n = 5"));
    QVERIFY(!box->isEditing());
    QVERIFY(anythingDrawn(rig.window, QRectF(200, 200, 110, 30)));
}

void PageTest::toolKeysGoIntoTheText()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(200, 200));

    // v/d/e/a and the digits are tool keys everywhere else; here they type.
    type(rig.window, "vd1e2a");
    QVERIFY(rig.page->editing());
    QCOMPARE(rig.page->items().size(), 0);
    QCOMPARE(rig.tools.tool(), Tools::Text);

    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("vd1e2a"));
}

void PageTest::escapeTwiceReturnsToSelect()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(200, 200));
    type(rig.window, "x");

    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.page->items().size(), 1); // the box stays
    QCOMPARE(rig.tools.tool(), Tools::Text);
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.tools.tool(), Tools::Select);

    // The same two-Esc round for an empty box, which is not even committed.
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(400, 300));
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(rig.tools.tool(), Tools::Text);
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.tools.tool(), Tools::Select);
}

void PageTest::emptyBoxRemovedWithoutAnUndoStep()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(400, 300));
    QTest::keyClick(&rig.window, Qt::Key_Escape);

    QCOMPARE(rig.page->items().size(), 1); // only the stroke: no box was added
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0); // the undo hits the draw itself
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 1);
}

void PageTest::createUndoRedo()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    TextBox *box = placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0); // the whole box, text included
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(rig.page->items().constFirst(), box);
    QCOMPARE(box->text(), QStringLiteral("ab"));
}

void PageTest::clickOnABoxEditsAtTheClick()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));

    QTest::keyClick(&rig.window, Qt::Key_T);
    // Inside the first glyph: the caret must start after the "a".
    click(rig.window, QPointF(208, 212));
    QVERIFY(rig.page->editing());
    QCOMPARE(rig.page->editing()->cursorPosition(), 1);
    type(rig.window, "X");
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("aXb"));
}

void PageTest::editUndoRedo()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(208, 212));
    type(rig.window, "X");
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("aXb"));

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 1); // the edit step, not the create
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("aXb"));
    // Two more undos step back through the edit and then the create.
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0);
}

void PageTest::ctrlZUndoesInsideTheBoxWhileEditing()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(208, 212));
    type(rig.window, "X");

    // While editing, Ctrl+Z belongs to the box (Qt's own text undo)...
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("ab"));
    QCOMPARE(rig.page->items().size(), 1);
    // ...and the page's stack was untouched: the next undo is the create.
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 1);
}

void PageTest::enterAddsALine()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(200, 200));
    type(rig.window, "ab");
    QTest::keyClick(&rig.window, Qt::Key_Return);
    type(rig.window, "cd");
    QTest::keyClick(&rig.window, Qt::Key_Escape);

    auto *box = static_cast<TextBox *>(rig.page->items().constFirst());
    QCOMPARE(box->text(), QStringLiteral("ab\ncd"));
    QVERIFY(box->bounds().height() > 30); // two lines, not one
}

void PageTest::boxSelectsMovesResizesDeletes()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    TextBox *box = placeBox(rig, QPointF(200, 200), QStringLiteral("abcd"));
    QTest::keyClick(&rig.window, Qt::Key_V);

    click(rig.window, QPointF(210, 210));
    QCOMPARE(rig.page->selection().size(), 1);
    QCOMPARE(rig.page->selection().constFirst(), static_cast<PageItem *>(box));

    const QPointF pos = box->position();
    drag(rig.window, QPointF(210, 210), QPointF(260, 240));
    QCOMPARE(box->position(), pos + QPointF(50, 30));

    // A corner drag scales the font, it never stretches the text.
    const qreal sizeBefore = box->fontSize();
    const QRectF rect = rig.page->selectionRect();
    drag(rig.window, rect.bottomRight(), rect.bottomRight() + QPointF(60, 60));
    QVERIFY(box->fontSize() > sizeBefore + 3);
    QVERIFY(rig.page->selectionRect().width() > rect.width() + 20);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(qAbs(box->fontSize() - sizeBefore) < 0.01);

    QTest::keyClick(&rig.window, Qt::Key_Delete);
    QCOMPARE(rig.page->items().size(), 0);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(rig.page->items().constFirst(), static_cast<PageItem *>(box));
    QCOMPARE(box->text(), QStringLiteral("abcd"));
}

void PageTest::doubleClickEditsABox()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_V);

    QTest::mouseDClick(&rig.window, Qt::LeftButton, {}, QPoint(208, 212));
    QVERIFY(rig.page->editing());
    QCOMPARE(rig.page->editing()->cursorPosition(), 1);
    type(rig.window, "X");
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("aXb"));
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.tools.tool(), Tools::Select);
}

void PageTest::clickAwayCommitsAndPlacesANewBox()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(200, 200));
    type(rig.window, "ab");
    click(rig.window, QPointF(500, 400)); // commits and places a new one

    QCOMPARE(rig.page->items().size(), 1);
    QVERIFY(rig.page->editing());
    QVERIFY(rig.page->editing() != rig.page->items().constFirst());
    type(rig.window, "cd");
    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.page->items().size(), 2);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("ab"));
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constLast())->text(),
             QStringLiteral("cd"));
}

void PageTest::typingIsFastWith200Boxes()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    // 200 boxes straight on the page, as after a long session.
    for (int i = 0; i < 200; ++i) {
        auto *box = new TextBox;
        box->setPosition(QPointF(20 + (i % 20) * 40, 20 + (i / 20) * 40));
        box->setText(QStringLiteral("x"));
        rig.page->addItem(box);
    }

    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(600, 430));
    QElapsedTimer clock;
    clock.start();
    type(rig.window, "hello world");
    const qint64 ms = clock.elapsed();
    QTest::keyClick(&rig.window, Qt::Key_Escape);

    QCOMPARE(rig.page->items().size(), 201);
    QVERIFY2(ms < 1000,
             qPrintable(QStringLiteral("typing 11 chars beside 200 boxes took %1 ms")
                            .arg(ms)));
}

void PageTest::textBoxTypesInJetBrainsMono()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(200, 200));
    type(rig.window, "mmiim");

    // While editing the font lives in the editor; it must be the mono the
    // design calls for, whose every glyph shares one advance width.
    const QFont editing = rig.page->editing()->font();
    QCOMPARE(QFontInfo(editing).family(), QStringLiteral("JetBrainsMono Nerd Font"));
    const QFontMetricsF whileEditing(editing);
    QCOMPARE(whileEditing.horizontalAdvance(u'i'),
             whileEditing.horizontalAdvance(u'm'));
    QVERIFY(qAbs(whileEditing.horizontalAdvance(u'i') - 11.4) < 0.1); // 0.6 em

    QTest::keyClick(&rig.window, Qt::Key_Escape);
    auto *box = static_cast<TextBox *>(rig.page->items().constFirst());
    QCOMPARE(QFontInfo(box->font()).family(), QStringLiteral("JetBrainsMono Nerd Font"));
    const QFontMetricsF committed(box->font());
    QCOMPARE(committed.horizontalAdvance(u'i'), committed.horizontalAdvance(u'm'));
    QCOMPARE(box->font().pixelSize(), 19);
}

void PageTest::saveAndFreshPageCommitTheEdit()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QSignalSpy saves(&rig.tools, &Tools::pathBarRequested);
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(200, 200));
    type(rig.window, "ab");

    // Ctrl+Shift+S works while typing: the edit commits first, then the bar.
    QTest::keyClick(&rig.window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(saves.size(), 1);
    QCOMPARE(saves.constFirst().constFirst().toString(), QStringLiteral("save"));
    QCOMPARE(rig.page->items().size(), 1);
    QVERIFY(!rig.page->editing());
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("ab"));

    // Edit again, then Ctrl+N: commit, then a fresh page — undoable.
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(225, 212)); // past the end: caret at "ab|"
    type(rig.window, "c");
    QTest::keyClick(&rig.window, Qt::Key_N, Qt::ControlModifier);
    QVERIFY(!rig.page->editing());
    QCOMPARE(rig.page->items().size(), 0);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("abc"));
}

void PageTest::warmTextEditorLeavesNoTrace()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));
    const bool cleanBefore = rig.page->isClean(); // the box's own AddItem

    rig.page->warmTextEditor();
    QCOMPARE(rig.page->items().size(), 1); // only the placed box
    QVERIFY(rig.page->editing() == nullptr);
    QCOMPARE(rig.page->isClean(), cleanBefore); // no undo step added
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("ab"));
}

void PageTest::quitWhileEditingAnExistingBoxFreesItOnce()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    TextBox *box = placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_V);
    QTest::mouseDClick(&rig.window, Qt::LeftButton, {}, QPoint(208, 212));
    QVERIFY(rig.page->editing());

    // The committed box is owned by its AddItem on the stack; quitting must
    // not free it a second time from the page.
    bool freed = false;
    QObject::connect(box, &QObject::destroyed, this, [&freed] { freed = true; });
    delete rig.page;
    rig.page = nullptr;
    QVERIFY(freed);
}

void PageTest::quitWhileEditingANewBoxFreesIt()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(200, 200));
    QVERIFY(rig.page->editing());
    TextBox *box = rig.page->editing();

    // The control: a box never committed has no other owner, so the page
    // frees it.
    bool freed = false;
    QObject::connect(box, &QObject::destroyed, this, [&freed] { freed = true; });
    delete rig.page;
    rig.page = nullptr;
    QVERIFY(freed);
}

void PageTest::emptyingAnExistingBoxRemovesItUndoable()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_V);
    QTest::mouseDClick(&rig.window, Qt::LeftButton, {}, QPoint(208, 212));
    QVERIFY(rig.page->editing());
    QTest::keyClick(&rig.window, Qt::Key_A, Qt::ControlModifier); // in the box
    QTest::keyClick(&rig.window, Qt::Key_Backspace);
    QTest::keyClick(&rig.window, Qt::Key_Escape);

    QCOMPARE(rig.page->items().size(), 0); // an emptied box does not linger
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 1); // one step: box and text come back
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 0);
}

void PageTest::boundsFollowTheLiveTextWhileEditing()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    TextBox *box = placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(225, 212)); // selects the box (first press)
    const qreal widthBefore = box->bounds().width();
    QTest::mouseDClick(&rig.window, Qt::LeftButton, {}, QPoint(225, 212));
    QVERIFY(rig.page->editing());
    const QRectF selectionBefore = rig.page->selectionRect();

    type(rig.window, "cdef");
    // The box grows as the text does: its bounds, the selection box around
    // it and the page's itemAt all follow before the commit.
    QVERIFY(box->bounds().width() > widthBefore + 30);
    QVERIFY(rig.page->selectionRect().width() > selectionBefore.width() + 30);
    QCOMPARE(rig.page->itemAt(QPointF(200 + widthBefore + 20, 210)),
             static_cast<PageItem *>(box));
    QVERIFY(rig.page->drawingBounds().width() > widthBefore + 30);

    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(box->text(), QStringLiteral("abcdef"));
}

void PageTest::selectToolClickAwayCommitsTheEdit()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    placeBox(rig, QPointF(200, 200), QStringLiteral("ab"));
    QTest::keyClick(&rig.window, Qt::Key_V);
    QTest::mouseDClick(&rig.window, Qt::LeftButton, {}, QPoint(208, 212));
    QVERIFY(rig.page->editing());
    type(rig.window, "x");

    click(rig.window, QPointF(500, 400)); // away: commits like the text tool
    QVERIFY(!rig.page->editing());
    QCOMPARE(rig.tools.tool(), Tools::Select);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("axb"));
}

void PageTest::zoomKeysWorkWhileTyping()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(200, 200));
    type(rig.window, "ab");

    // Zooming is a view change: it never commits the edit, like the wheel.
    QTest::keyClick(&rig.window, Qt::Key_Equal, Qt::ControlModifier);
    QTest::qWait(300); // the step eases in
    QCOMPARE(rig.page->zoom(), 1.25);
    QVERIFY(rig.page->editing());
    QTest::keyClick(&rig.window, Qt::Key_Minus, Qt::ControlModifier);
    QTest::qWait(300);
    QCOMPARE(rig.page->zoom(), 1.0);
    QTest::keyClick(&rig.window, Qt::Key_Equal, Qt::ControlModifier);
    QTest::qWait(300);
    QCOMPARE(rig.page->zoom(), 1.25);
    QTest::keyClick(&rig.window, Qt::Key_0, Qt::ControlModifier);
    QTest::qWait(300);
    QCOMPARE(rig.page->zoom(), 1.0);
    QVERIFY(rig.page->editing());
    type(rig.window, "-0"); // without Ctrl these are text

    QTest::keyClick(&rig.window, Qt::Key_Escape);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(static_cast<TextBox *>(rig.page->items().constFirst())->text(),
             QStringLiteral("ab-0"));
}

void PageTest::arrowDrawsShaftAndHeadWithUndoRedo()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_A);
    QCOMPARE(rig.tools.tool(), Tools::Arrow);

    drag(rig.window, QPointF(100, 300), QPointF(400, 300));

    QCOMPARE(rig.page->items().size(), 1);
    QVERIFY(qobject_cast<Arrow *>(rig.page->items().constFirst()));
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::ink); // the shaft
    QCOMPARE(pixel(rig.window, QPointF(395, 300)), palette::ink); // the head
    QCOMPARE(pixel(rig.window, QPointF(395, 304)), palette::page); // past its slant

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0);
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::page);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(pixel(rig.window, QPointF(395, 300)), palette::ink);
}

void PageTest::arrowShiftSnapsTo45Degrees()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_A);

    // A near-horizontal drag snaps flat: no ink where the raw line would run.
    mouse(rig.window, QEvent::MouseButtonPress, QPointF(150, 350));
    mouse(rig.window, QEvent::MouseMove, QPointF(350, 356), Qt::ShiftModifier);
    mouse(rig.window, QEvent::MouseButtonRelease, QPointF(350, 356),
          Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(pixel(rig.window, QPointF(200, 350)), palette::ink);
    QCOMPARE(pixel(rig.window, QPointF(345, 350)), palette::ink); // the head
    QCOMPARE(pixel(rig.window, QPointF(200, 352)), palette::page);
    QCOMPARE(pixel(rig.window, QPointF(345, 355)), palette::page);

    // A steep drag snaps to -45°: the line runs up at exactly that slant.
    mouse(rig.window, QEvent::MouseButtonPress, QPointF(150, 400));
    mouse(rig.window, QEvent::MouseMove, QPointF(350, 190), Qt::ShiftModifier);
    mouse(rig.window, QEvent::MouseButtonRelease, QPointF(350, 190),
          Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 2);
    QCOMPARE(pixel(rig.window, QPointF(252, 297)), palette::ink);
}

void PageTest::arrowClickWithoutDragMakesNothing()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 250}, {150, 250}, {200, 250}, {250, 250}, {300, 250}});
    QTest::keyClick(&rig.window, Qt::Key_A);

    click(rig.window, QPointF(400, 300));
    QCOMPARE(rig.page->items().size(), 1); // the click made nothing

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0); // and minted no undo step of its own
}

void PageTest::arrowHitTestsNearTheInkNotTheBox()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_A);
    drag(rig.window, QPointF(200, 200), QPointF(400, 200));
    auto *horizontal = static_cast<Arrow *>(rig.page->items().constFirst());

    // With a 2 px tolerance the shaft alone cannot reach the head, yet the
    // head is ink; just past its slant or before its base, nothing.
    QVERIFY(horizontal->hitTest(QPointF(386, 204.5), 2));
    QVERIFY(!horizontal->hitTest(QPointF(386, 205.6), 2));
    QVERIFY(!horizontal->hitTest(QPointF(378, 203), 2));

    drag(rig.window, QPointF(100, 100), QPointF(300, 300)); // a diagonal one
    QTest::keyClick(&rig.window, Qt::Key_V);

    click(rig.window, QPointF(150, 250)); // inside its box, 70 px off the line
    QCOMPARE(rig.page->selection().size(), 0);

    click(rig.window, QPointF(150, 148)); // 1.4 px off the diagonal: a hit
    QCOMPARE(rig.page->selection().size(), 1);
    QCOMPARE(rig.page->selection().constFirst(), rig.page->items().constLast());
}

void PageTest::arrowMarqueeSelectsThroughTheHeadAlone()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_A);
    drag(rig.window, QPointF(200, 200), QPointF(400, 200));
    QTest::keyClick(&rig.window, Qt::Key_V);

    // The press is off the ink (well past the 6 px click tolerance), so the
    // drag is a marquee; its box reaches the head's lower corner only.
    drag(rig.window, QPointF(383, 212), QPointF(386.5, 204.8));
    QCOMPARE(rig.page->selection().size(), 1);

    click(rig.window, QPointF(500, 400)); // clear, then a box off all ink
    drag(rig.window, QPointF(370, 210), QPointF(381, 214));
    QCOMPARE(rig.page->selection().size(), 0);
}

void PageTest::arrowMinDragIsScreenPixels()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_A);
    Page *page = rig.page;
    const auto onScreen = [page](QPointF world) {
        return world * page->zoom() + page->worldPos();
    };

    // Zoomed in, a drag can be under 4 world px yet over 4 screen px: drawn.
    pinch(rig.window, QPointF(320, 240), 1.0); // zoom 2
    QCOMPARE(page->zoom(), 2.0);
    drag(rig.window, onScreen(QPointF(300, 200)), onScreen(QPointF(303, 200)));
    QCOMPARE(rig.page->items().size(), 1); // 3 world px = 6 screen px

    // Zoomed out, 6 world px are 3 screen px: nothing.
    pinch(rig.window, QPointF(320, 240), -0.75); // zoom 0.5
    QCOMPARE(page->zoom(), 0.5);
    drag(rig.window, onScreen(QPointF(100, 100)), onScreen(QPointF(106, 100)));
    QCOMPARE(rig.page->items().size(), 1);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0); // the zoom-2 arrow was the only step
}

void PageTest::arrowMovesResizesDeletesLikeAStroke()
{
    Rig rig;
    rig.window.show();
    QTest::qWait(50);
    QTest::keyClick(&rig.window, Qt::Key_A);
    drag(rig.window, QPointF(200, 200), QPointF(400, 200));
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(300, 200)); // on the shaft
    QCOMPARE(rig.page->selection().size(), 1);

    // Moving: the whole arrow follows the pointer, geometry untouched.
    drag(rig.window, QPointF(300, 200), QPointF(340, 230));
    auto *arrow = static_cast<Arrow *>(rig.page->items().constFirst());
    QCOMPARE(arrow->position(), QPointF(40, 30));
    QCOMPARE(pixel(rig.window, QPointF(340, 230)), palette::ink);
    QCOMPARE(pixel(rig.window, QPointF(300, 200)), palette::page);

    QTest::keyClick(&rig.window, Qt::Key_Delete);
    QCOMPARE(rig.page->items().size(), 0);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(pixel(rig.window, QPointF(340, 230)), palette::ink);

    // Resizing bakes into the endpoints once; the line width stays.
    click(rig.window, QPointF(340, 230));
    const int before = inkRows(rig.window, 340);
    const QRectF rect = rig.page->selectionRect();
    drag(rig.window, rect.bottomRight(), rect.bottomRight() + QPointF(60, 60));
    const int after = inkRows(rig.window, 340);
    QVERIFY(before >= 2);
    QVERIFY(qAbs(after - before) <= 1);

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(pixel(rig.window, QPointF(340, 230)), palette::ink);
}

QTEST_MAIN(PageTest)
#include "tst_page.moc"
