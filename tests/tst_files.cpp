// Saving, opening, the clipboard and image items, against a Page built the
// way main.cpp builds it and HOME pointed at a temp folder, so nothing
// touches the real ~/Pictures.
#include <QtGui/QClipboard>
#include <QElapsedTimer>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>
#include <QtQuick/QQuickRenderControl>
#include <rhi/qrhi.h>
#include <QtQuick/QQuickWindow>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "files.h"
#include "imageitem.h"
#include "palette.h"
#include "page.h"
#include "stroke.h"
#include "textbox.h"
#include "tools.h"

namespace {

// Window + page + tools + files, wired the way main.cpp wires them.
struct Rig
{
    QQuickWindow window;
    Tools tools;
    Files files;
    QQmlEngine engine;
    Page *page = nullptr;

    explicit Rig(QSizeF size = QSizeF(640, 480))
    {
        window.resize(size.toSize());
        window.setColor(palette::page);
        page = new Page(window.contentItem());
        // The text editor is created through the page's QML context, like
        // the engine-loaded page main.cpp works with.
        QQmlEngine::setContextForObject(
            page, new QQmlContext(engine.rootContext()));
        page->setSize(size);
        tools.attach(&window);
        tools.setPage(page);
        files.setPage(page);
        window.show();
        // Wait for the first rendered frame, so the render thread and its
        // GL context are settled before anything renders offscreen.
        QElapsedTimer waited;
        waited.start();
        bool frameDone = false;
        QObject::connect(&window, &QQuickWindow::frameSwapped, &window,
                         [&] { frameDone = true; }, Qt::SingleShotConnection);
        while (!frameDone && waited.elapsed() < 2000)
            QTest::qWait(10);
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

void mouse(QQuickWindow &window, QEvent::Type type, const QPointF &pos,
           Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent event(type, pos, window.mapToGlobal(pos),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      mods);
    QGuiApplication::sendEvent(&window, &event);
}

void click(QQuickWindow &window, const QPointF &pos)
{
    mouse(window, QEvent::MouseButtonPress, pos);
    mouse(window, QEvent::MouseButtonRelease, pos);
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

// The offscreen cursor only synthesizes a hover on the first move; post the
// hover event directly, like tst_page does.
void hover(QQuickWindow &window, const QPointF &pos)
{
    QHoverEvent event(QEvent::HoverMove, pos, window.mapToGlobal(pos), QPointF());
    QGuiApplication::sendEvent(&window, &event);
}

QColor pixel(QQuickWindow &window, const QPointF &pos)
{
    const QImage image = window.grabWindow();
    const qreal dpr = image.devicePixelRatio();
    return image.pixelColor(qRound(pos.x() * dpr), qRound(pos.y() * dpr));
}

// A solid red PNG of the given size, written to path.
void writeTestPng(const QString &path, const QSize &size)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QDir().mkpath(QFileInfo(path).path());
    QVERIFY(image.save(path, "PNG"));
}

QStringList savedNames(const QTemporaryDir &home, const QString &sub = "Pictures/Drawings")
{
    return QDir(home.filePath(sub)).entryList(QDir::Files);
}

QColor pngPixel(const QString &path, const QPoint &pos)
{
    const QImage png(path);
    return png.pixelColor(pos);
}

} // namespace

class FilesTest : public QObject
{
    Q_OBJECT

private slots:
    void exportCropsToTheDrawingWhereverTheViewSits();
    void ctrlSChoosesADefaultPathThenOverwritesIt();
    void freshPageForgetsTheSaveTarget();
    void saveAsMovesTheTarget();
    void emptyPageWritesNothing();
    void exportBeyondTheTextureLimitCapsTheScale();
    void openPlacesTheImageCentredAndUndoable();
    void openShrinksABiggerImageToFit();
    void hugeImagesLoadDecimated();
    void clipboardRoundTrip();
    void pasteLandsAtTheMouseWhereverTheViewSits();
    void imageItemMovesResizesErasesUndoes();
    void recolourSkipsImages();
    void startupOpenLeavesThePageClean();
    void closingCommitsTheBoxBeingEdited();
    void closingAutosavesOnlyUnsavedChanges();

private:
    // Each test gets its own empty HOME, so nothing touches ~/Pictures and
    // the default folder starts out empty.
    QTemporaryDir m_home;
    void freshHome()
    {
        m_home = QTemporaryDir();
        QVERIFY(m_home.isValid());
        qputenv("HOME", m_home.path().toUtf8());
    }
};

void FilesTest::exportCropsToTheDrawingWhereverTheViewSits()
{
    freshHome();
    Rig rig;
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    QTest::keyClick(&rig.window, Qt::Key_A);
    draw(rig.window, {{100, 320}, {300, 320}});
    const QRectF bounds = rig.page->drawingBounds();
    QVERIFY(bounds.isValid());

    // Pan and zoom: the export must not care where the view sits.
    wheel(rig.window, QPointF(320, 240), QPoint(0, 120));
    QCOMPARE(rig.page->worldPos(), QPointF(0, 120));
    wheel(rig.window, QPointF(320, 240), QPoint(0, 120), Qt::ControlModifier);
    QCOMPARE(rig.page->zoom(), 1.25);

    QSignalSpy toast(&rig.files, &Files::toastRequested);
    rig.files.save();

    QCOMPARE(toast.size(), 1);
    QVERIFY(toast.constFirst().constFirst().toString().startsWith("saved → ~/"));
    const QStringList names = savedNames(m_home);
    QCOMPARE(names.size(), 1);
    QVERIFY(QRegularExpression(R"(^\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}\.png$)")
                .match(names.constFirst()).hasMatch());

    // The PNG covers the drawing plus a 24 px margin on each side.
    const QString path = QDir(m_home.filePath("Pictures/Drawings")).filePath(names.constFirst());
    const QImage png(path);
    const QSize expected(qCeil(bounds.width() + 2 * kExportMargin),
                         qCeil(bounds.height() + 2 * kExportMargin));
    QCOMPARE(png.size(), expected);

    // Stroke and arrow (shaft and head alike) are ink in the PNG; the
    // margin stays white.
    const auto worldInPng = [&bounds](qreal x, qreal y) {
        return QPoint(qRound(x - bounds.left() + kExportMargin),
                      qRound(y - bounds.top() + kExportMargin));
    };
    QCOMPARE(pngPixel(path, worldInPng(200, 300)), palette::ink); // the stroke
    QCOMPARE(pngPixel(path, worldInPng(200, 320)), palette::ink); // arrow shaft
    QCOMPARE(pngPixel(path, worldInPng(294, 320)), palette::ink); // arrow head
    QCOMPARE(pngPixel(path, {0, 0}), palette::page);
    QCOMPARE(pngPixel(path, {png.width() - 1, png.height() - 1}), palette::page);
}

void FilesTest::ctrlSChoosesADefaultPathThenOverwritesIt()
{
    freshHome();
    Rig rig;
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    rig.files.save();
    const QStringList first = savedNames(m_home);
    QCOMPARE(first.size(), 1);
    const QString path = QDir(m_home.filePath("Pictures/Drawings")).filePath(first.constFirst());
    QFile firstFile(path);
    QVERIFY(firstFile.open(QIODevice::ReadOnly));
    const QByteArray before = firstFile.readAll();
    firstFile.close();

    draw(rig.window, {{100, 400}, {150, 400}, {200, 400}, {250, 400}, {300, 400}});
    rig.files.save(); // same second, same file: overwritten, not duplicated

    QCOMPARE(savedNames(m_home), first);
    QFile secondFile(path);
    QVERIFY(secondFile.open(QIODevice::ReadOnly));
    QVERIFY(secondFile.readAll() != before);
}

// Ctrl+N throws the drawing away (undoable), so the next save must start a
// new file instead of silently overwriting the previous drawing's target.
void FilesTest::freshPageForgetsTheSaveTarget()
{
    freshHome();
    Rig rig;
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    const QString path = m_home.filePath("first.png");
    rig.files.confirm(path, "save");
    QCOMPARE(rig.files.saveTarget(), path);
    const QByteArray before = [&path] {
        QFile file(path);
        file.open(QIODevice::ReadOnly);
        return file.readAll();
    }();

    QTest::keyClick(&rig.window, Qt::Key_N, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0);
    QCOMPARE(rig.files.saveTarget(), QString()); // forgotten

    draw(rig.window, {{100, 100}, {150, 100}, {200, 100}});
    rig.files.save(); // a new timestamped file, never first.png

    QVERIFY(rig.files.saveTarget() != path);
    QCOMPARE(savedNames(m_home).size(), 1);
    QFile firstFile(path);
    firstFile.open(QIODevice::ReadOnly);
    QCOMPARE(firstFile.readAll(), before); // untouched
}

void FilesTest::saveAsMovesTheTarget()
{
    freshHome();
    Rig rig;
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    const QString path = m_home.filePath("notes/scratch/idea.png");
    rig.files.confirm(path, "save"); // missing folders are created
    QVERIFY(QFile(path).exists());

    draw(rig.window, {{100, 400}, {150, 400}, {200, 400}, {250, 400}, {300, 400}});
    rig.files.save(); // the confirmed path became the target of later Ctrl+S
    QCOMPARE(rig.files.saveTarget(), path);
    QVERIFY(!QDir(m_home.filePath("Pictures/Drawings")).exists());
    QVERIFY(rig.page->isClean());
}

// A stroke impossibly far out used to fail the whole save: the export's
// render target exceeded the device's largest texture and came back empty.
// The scale now caps so the longest side fits, and the page saves smaller.
void FilesTest::exportBeyondTheTextureLimitCapsTheScale()
{
    freshHome();
    Rig rig;
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}});
    Stroke *far = new Stroke;
    far->setColor(palette::ink);
    far->setStrokeWidth(2.75);
    far->begin(QPointF(50000, 400));
    far->addPoint(QPointF(50040, 400));
    rig.page->addItem(far);

    QSignalSpy toast(&rig.files, &Files::toastRequested);
    rig.files.save();
    QCOMPARE(toast.size(), 1);
    QVERIFY(toast.constFirst().constFirst().toString().startsWith("saved → ~/"));

    QQuickRenderControl probe;
    QQuickWindow probeWindow(&probe);
    QVERIFY(probe.initialize());
    const int limit = probe.rhi()->resourceLimit(QRhi::TextureSizeMax);

    const QStringList names = savedNames(m_home);
    QCOMPARE(names.size(), 1);
    const QImage png(QDir(m_home.filePath("Pictures/Drawings"))
                         .filePath(names.constFirst()));
    QVERIFY(!png.isNull());
    QVERIFY(png.width() <= limit);
    QVERIFY(png.height() > 0);
    // The cap shrinks the whole export, it does not crop the far stroke
    // away: both strokes' ink is where the shrink puts it (within a
    // couple of pixels; a 2.75 px stroke is under 1 px at this scale).
    const QRectF bounds = rig.page->drawingBounds();
    const qreal scale = png.width() / qreal(bounds.width() + 2 * kExportMargin);
    QVERIFY(scale < 1.0);
    const auto worldInPng = [&bounds, scale](qreal x, qreal y) {
        return QPoint(qRound((x - bounds.left()) * scale + kExportMargin * scale),
                      qRound((y - bounds.top()) * scale + kExportMargin * scale));
    };
    const auto inkNear = [&png](QPoint p) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                if (png.pixelColor(p + QPoint(dx, dy)) != palette::page)
                    return true;
        return false;
    };
    QVERIFY(inkNear(worldInPng(150, 300)));
    QVERIFY(inkNear(worldInPng(50020, 400)));
}

void FilesTest::emptyPageWritesNothing()
{
    freshHome();
    Rig rig;
    QSignalSpy toast(&rig.files, &Files::toastRequested);

    rig.files.save();
    QCOMPARE(toast.size(), 1);
    QCOMPARE(toast.constFirst().constFirst().toString(), "nothing to save");
    QVERIFY(!QDir(m_home.filePath("Pictures")).exists());

    rig.files.appClosing();
    QVERIFY(!QDir(m_home.filePath("Pictures")).exists());
}

void FilesTest::openPlacesTheImageCentredAndUndoable()
{
    freshHome();
    Rig rig;
    const QString path = m_home.filePath("in.png");
    writeTestPng(path, {80, 60});

    rig.files.confirm(path, "open");
    QCOMPARE(rig.page->items().size(), 1);
    auto *image = qobject_cast<ImageItem *>(rig.page->items().constFirst());
    QVERIFY(image);
    QCOMPARE(image->size(), QSizeF(80, 60)); // 1:1, smaller than the view
    QCOMPARE(image->position(), QPointF(320 - 40, 240 - 30)); // centred

    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 0);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(rig.page->items().size(), 1);

    // A drop (openDrop, the other openFile entry) loads the same way.
    rig.page->undo();
    QCOMPARE(rig.page->items().size(), 0);
    rig.files.openDrop(QUrl::fromLocalFile(path));
    QCOMPARE(rig.page->items().size(), 1);

    // A non-image file says so and adds nothing.
    const QString text = m_home.filePath("no.png");
    {
        QFile file(text);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not an image");
    }
    QSignalSpy toast(&rig.files, &Files::toastRequested);
    rig.files.openDrop(QUrl::fromLocalFile(text));
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(toast.size(), 1);
}

void FilesTest::openShrinksABiggerImageToFit()
{
    freshHome();
    Rig rig;
    const QString path = m_home.filePath("big.png");
    writeTestPng(path, {2000, 1200});

    rig.files.confirm(path, "open");
    QCOMPARE(rig.page->items().size(), 1);
    auto *image = qobject_cast<ImageItem *>(rig.page->items().constFirst());
    QVERIFY(image);
    // 0.9 * view / image, kept proportional, centred in the view.
    QVERIFY(qAbs(image->width() - 576.0) < 0.01);
    QVERIFY(qAbs(image->height() - 345.6) < 0.01);
    QCOMPARE(image->bounds().center(), QPointF(320, 240));
}

void FilesTest::hugeImagesLoadDecimated()
{
    freshHome();
    Rig rig;
    // A 4000x3000 screenshot opened into the 640x480 view shows at 576x432;
    // the texture is cut to twice that (the dpr is 1 offscreen), instead of
    // the 48 MB the full decode would keep on the GPU.
    const QString path = m_home.filePath("huge.png");
    writeTestPng(path, {4000, 3000});
    rig.files.confirm(path, "open");
    auto *image = qobject_cast<ImageItem *>(rig.page->items().constFirst());
    QVERIFY(image);
    QCOMPARE(image->width(), 576.0);
    QCOMPARE(image->height(), 432.0);
    QCOMPARE(image->image().size(), QSize(1152, 864));

    // A big paste gets the same treatment; small pastes stay 1:1
    // (clipboardRoundTrip pins that).
    QImage big(3000, 2000, QImage::Format_ARGB32);
    big.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(big);
    hover(rig.window, QPointF(200, 200));
    rig.files.paste();
    auto *pasted = qobject_cast<ImageItem *>(rig.page->items().constLast());
    QVERIFY(pasted);
    QCOMPARE(pasted->width(), 576.0); // shrunk to fit, like an open
    QCOMPARE(pasted->height(), 384.0);
    QCOMPARE(pasted->image().size(), QSize(1152, 768));
}

void FilesTest::clipboardRoundTrip()
{
    freshHome();
    Rig rig;
    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(source);

    hover(rig.window, QPointF(200, 200));
    rig.files.paste();
    QCOMPARE(rig.page->items().size(), 1);
    PageItem *image = rig.page->items().constFirst();
    QCOMPARE(image->position(), QPointF(200, 200)); // top-left at the mouse
    QCOMPARE(image->size(), QSizeF(64, 48));

    // Copy the selection: a PNG of its bounds plus margin, on white.
    QTest::keyClick(&rig.window, Qt::Key_V); // the select tool
    click(rig.window, QPointF(230, 220));
    QCOMPARE(rig.page->selection().size(), 1);
    rig.files.copySelection();
    const QImage copied = QGuiApplication::clipboard()->image();
    QCOMPARE(copied.size(), QSize(64 + 2 * 24, 48 + 2 * 24));
    QCOMPARE(copied.pixelColor(29, 29), QColor(Qt::red));
    QCOMPARE(copied.pixelColor(0, 0), palette::page);

    // The copied PNG pastes back as an image item.
    rig.files.paste();
    QCOMPARE(rig.page->items().size(), 2);
}

void FilesTest::pasteLandsAtTheMouseWhereverTheViewSits()
{
    freshHome();
    Rig rig;
    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(source);

    // Panned and zoomed: the top-left must be the world point under the
    // mouse, which is (mouse - worldPos) / zoom, not worldPos + mouse/zoom.
    wheel(rig.window, QPointF(320, 240), QPoint(0, 120));
    wheel(rig.window, QPointF(320, 240), QPoint(0, 120), Qt::ControlModifier);
    QCOMPARE(rig.page->zoom(), 1.25);
    hover(rig.window, QPointF(200, 200));
    rig.files.paste();

    QCOMPARE(rig.page->items().size(), 1);
    PageItem *image = rig.page->items().constFirst();
    const QPointF expected = (QPointF(200, 200) - rig.page->worldPos())
                                 / rig.page->zoom();
    QCOMPARE(image->position(), expected);
    QCOMPARE(image->size(), QSizeF(64, 48));
}

void FilesTest::imageItemMovesResizesErasesUndoes()
{
    freshHome();
    Rig rig;
    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(source);
    hover(rig.window, QPointF(200, 200));
    rig.files.paste();
    PageItem *image = rig.page->items().constFirst();

    // Select with a click inside, move with a drag.
    QTest::keyClick(&rig.window, Qt::Key_V);
    click(rig.window, QPointF(230, 220));
    QCOMPARE(rig.page->selection().size(), 1);
    drag(rig.window, QPointF(230, 220), QPointF(290, 260));
    QCOMPARE(image->position(), QPointF(260, 240));
    QCOMPARE(image->size(), QSizeF(64, 48)); // moving never rescales

    // A corner drag keeps proportions, and undo reverts it once.
    const QRectF before = rig.page->selectionRect();
    drag(rig.window, before.bottomRight(), before.bottomRight() + QPointF(60, 60));
    QVERIFY(image->width() > 64);
    QVERIFY(qAbs(image->width() / image->height() - 64.0 / 48.0) < 0.01);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(image->size(), QSizeF(64, 48));
    QCOMPARE(image->position(), QPointF(260, 240));

    // The eraser takes the whole image; undo brings it back.
    QTest::keyClick(&rig.window, Qt::Key_E);
    drag(rig.window, QPointF(290, 260), QPointF(300, 270));
    QCOMPARE(rig.page->items().size(), 0);
    QTest::qWait(150); // the erased item fades for 80 ms first
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(rig.page->items().size(), 1);
    QCOMPARE(rig.page->items().constFirst(), image);
}

void FilesTest::recolourSkipsImages()
{
    freshHome();
    Rig rig;
    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(source);
    hover(rig.window, QPointF(400, 400));
    rig.files.paste();

    QTest::keyClick(&rig.window, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(rig.page->selection().size(), 2);

    // 1/2/3 recolour the stroke and leave the image alone — and mint no
    // extra undo step, so the first undo is the recolour itself.
    QTest::keyClick(&rig.window, Qt::Key_2);
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::red);
    QTest::keyClick(&rig.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(pixel(rig.window, QPointF(200, 300)), palette::ink);
    QCOMPARE(rig.page->items().size(), 2);
}

void FilesTest::startupOpenLeavesThePageClean()
{
    freshHome();
    Rig rig;
    const QString path = m_home.filePath("in.png");
    writeTestPng(path, {80, 60});

    // `omasketch file.png` then close without edits: the file exists on
    // disk, so closing must not autosave a duplicate next to it.
    rig.files.openStartupFile(path);
    QCOMPARE(rig.page->items().size(), 1);
    QVERIFY(rig.page->isClean());

    rig.files.appClosing();
    QVERIFY(!QDir(m_home.filePath("Pictures")).exists());
}

void FilesTest::closingCommitsTheBoxBeingEdited()
{
    freshHome();
    Rig rig;
    QTest::keyClick(&rig.window, Qt::Key_T);
    click(rig.window, QPointF(300, 300));
    QVERIFY(rig.page->editing());
    type(rig.window, QStringLiteral("scratch"));
    QVERIFY(savedNames(m_home).isEmpty());

    rig.files.appClosing(); // the commit lands on the undo stack first
    QCOMPARE(rig.page->items().size(), 1);
    auto *box = qobject_cast<TextBox *>(rig.page->items().constFirst());
    QVERIFY(box);
    QCOMPARE(box->text(), QStringLiteral("scratch"));
    QCOMPARE(savedNames(m_home).size(), 1); // the autosave carried the text
}

void FilesTest::closingAutosavesOnlyUnsavedChanges()
{
    freshHome();
    Rig rig;
    // A blank page is never saved.
    rig.files.appClosing();
    QVERIFY(!QDir(m_home.filePath("Pictures")).exists());

    QTest::keyClick(&rig.window, Qt::Key_D);
    draw(rig.window, {{100, 300}, {150, 300}, {200, 300}, {250, 300}, {300, 300}});
    rig.files.appClosing(); // quietly into the default folder
    QCOMPARE(savedNames(m_home).size(), 1);
    QVERIFY(rig.page->isClean());

    // Clean now: closing again writes nothing new.
    const QStringList before = savedNames(m_home);
    rig.files.appClosing();
    QCOMPARE(savedNames(m_home), before);

    // Unsaved edits go to the current target once there is one.
    const QString path = m_home.filePath("elsewhere.png");
    rig.files.confirm(path, "save");
    draw(rig.window, {{100, 400}, {150, 400}, {200, 400}, {300, 400}});
    rig.files.appClosing();
    QVERIFY(QFile(path).size() > 0);
    QCOMPARE(savedNames(m_home).size(), 1); // the default folder gained nothing
}

QTEST_MAIN(FilesTest)
#include "tst_files.moc"
