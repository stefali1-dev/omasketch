#include "files.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QtMath>
#include <rhi/qrhi.h>

#include "page.h"
#include "pageitem.h"
#include "palette.h"

QImage renderWorldToImage(QQuickItem *world, const QRectF &worldRect, qreal scale)
{
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setColor(palette::page);
    const QSize size(qCeil(worldRect.width() * scale), qCeil(worldRect.height() * scale));
    window.setGeometry(0, 0, size.width(), size.height());
    if (!control.initialize())
        return {};

    // The frame goes into a multisample renderbuffer when the RHI supports
    // it (the screen antialiases too), resolved into a plain texture that
    // the readback copies out; without MSAA support the texture is the
    // target itself. Declared after window and control, so they die first:
    // the RHI stays alive while its resources are destroyed, in the reverse
    // of this order.
    QRhi *rhi = control.rhi();
    const int samples = rhi->supportedSampleCounts().contains(4) ? 4 : 1;
    QRhiRenderBuffer *msaa = nullptr;
    if (samples > 1) {
        msaa = rhi->newRenderBuffer(QRhiRenderBuffer::Color, size, samples);
        if (!msaa->create()) {
            delete msaa;
            return {};
        }
    }
    QRhiTexture *flat = rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                        QRhiTexture::RenderTarget
                                            | QRhiTexture::UsedAsTransferSource);
    if (!flat->create()) {
        delete flat;
        delete msaa;
        return {};
    }
    QRhiColorAttachment colour = samples > 1 ? QRhiColorAttachment(msaa)
                                             : QRhiColorAttachment(flat);
    colour.setResolveTexture(samples > 1 ? flat : nullptr);
    QRhiTextureRenderTargetDescription description(colour);
    QRhiTextureRenderTarget *target = rhi->newTextureRenderTarget(description);
    QRhiRenderPassDescriptor *pass = target->newCompatibleRenderPassDescriptor();
    target->setRenderPassDescriptor(pass); // without it nothing renders
    if (!target->create()) {
        delete target;
        delete pass;
        delete flat;
        delete msaa;
        return {};
    }
    window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target));

    // The world item's transform origin is its top-left corner (page.cpp),
    // so position and scale map the world rect onto the whole target.
    QQuickItem *oldParent = world->parentItem();
    const QPointF oldPos = world->position();
    const qreal oldScale = world->scale();
    world->setParentItem(window.contentItem());
    world->setScale(scale);
    world->setPosition(-worldRect.topLeft() * scale);

    control.polishItems();
    control.beginFrame();
    control.sync();
    control.render();
    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture(flat, &readback);
    control.commandBuffer()->resourceUpdate(batch);
    control.endFrame(); // offscreen frames do not pipeline: the data is here
    if (readback.data.isEmpty())
        qWarning("omasketch: export readback came back empty");

    // Back exactly as it was; the visible page re-renders on its next frame.
    world->setParentItem(oldParent);
    world->setScale(oldScale);
    world->setPosition(oldPos);
    window.setRenderTarget(QQuickRenderTarget()); // before the target dies
    delete target;
    delete pass;

    const QImage raw(reinterpret_cast<const uchar *>(readback.data.constData()),
                     readback.pixelSize.width(), readback.pixelSize.height(),
                     QImage::Format_RGBA8888_Premultiplied);
    return rhi->isYUpInFramebuffer() ? raw.flipped() : raw.copy();
}

namespace {

// "/home/user/x" → "~/x", for the toast.
QString shortHome(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}

qreal exportScale(const Page *page)
{
    // At least the screen's device pixel ratio, so the PNG is as sharp as
    // the page is on screen.
    return page->window() ? qMax(1.0, page->window()->devicePixelRatio()) : 1.0;
}

// The size a new image item shows at: 1:1, shrunk to a little air inside
// the view when the image is bigger (open centres it; paste keeps the
// mouse corner).
QSizeF displayedSize(const QImage &image, const Page *page)
{
    QSizeF size(image.size());
    const qreal fit = qMin(1.0, qMin(0.9 * page->width() / size.width(),
                                     0.9 * page->height() / size.height()));
    if (fit < 1.0)
        size *= fit;
    return size;
}

// The texture only has to look sharp on screen: past twice the displayed
// size at the device pixel ratio nothing can show, so a huge screenshot is
// decimated at load instead of living on as a 256 MB GPU texture.
QImage atScreenResolution(const QImage &image, const QSizeF &displayed, const Page *page)
{
    const qreal dpr = page->window() ? qMax(1.0, page->window()->devicePixelRatio()) : 1.0;
    const qreal cap = 2 * dpr * qMax(displayed.width(), displayed.height());
    const qreal longest = qMax(image.width(), image.height());
    if (longest <= cap)
        return image;
    return image.scaled((QSizeF(image.size()) * (cap / longest)).toSize(),
                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace

Files::Files(QObject *parent)
    : QObject(parent)
{
}

void Files::setPage(Page *page)
{
    m_page = page;
}

void Files::save()
{
    saveTo(m_target.isEmpty() ? defaultPath() : m_target, true);
}

void Files::confirm(const QString &path, const QString &mode)
{
    if (mode == "open")
        openFile(path);
    else
        saveTo(path, true);
}

void Files::copySelection()
{
    QRectF world;
    const QList<PageItem *> selection = m_page->selection();
    if (!selection.isEmpty()) {
        for (PageItem *item : selection)
            world = world.isValid() ? world.united(item->bounds()) : item->bounds();
    } else {
        world = m_page->drawingBounds();
    }
    if (!world.isValid())
        return;
    const QRectF rect = world.adjusted(-kExportMargin, -kExportMargin,
                                       kExportMargin, kExportMargin);
    QGuiApplication::clipboard()->setImage(
        renderWorldToImage(m_page->worldItem(), rect, exportScale(m_page)));
}

void Files::paste()
{
    const QImage clipboard = QGuiApplication::clipboard()->image();
    if (clipboard.isNull())
        return;
    // Top-left at the mouse, so the image lands where the eye is; one
    // bigger than the view shrinks to fit, like an open.
    const QSizeF size = displayedSize(clipboard, m_page);
    const QPointF world = m_page->worldPos() + m_page->mouse() / m_page->zoom();
    const QImage image = atScreenResolution(clipboard, size, m_page);
    m_page->addImage(image, world, size);
}

void Files::openDrop(const QUrl &url)
{
    openFile(url.toLocalFile());
}

void Files::openFile(const QString &path)
{
    QImageReader reader(path);
    if (reader.canRead())
        openImage(reader.read());
    else
        emit toastRequested("no image at " + shortHome(path));
}

void Files::openStartupFile(const QString &path)
{
    openFile(path);
    m_page->markClean();
}

void Files::appClosing()
{
    // A box being edited commits first, so its text lands on the undo stack
    // and the autosave below writes it out too.
    m_page->commitEditing();
    if (m_page->isClean() || !m_page->drawingBounds().isValid())
        return; // a blank or already-saved page is never written
    saveTo(m_target.isEmpty() ? defaultPath() : m_target, false);
}

void Files::saveTo(const QString &path, bool toast)
{
    const QRectF rect = exportRect();
    if (!rect.isValid()) {
        if (toast)
            emit toastRequested("nothing to save");
        return;
    }
    if (writePng(rect, path)) {
        m_target = path;
        m_page->markClean();
        if (toast)
            emit toastRequested("saved → " + shortHome(path));
    } else if (toast) {
        emit toastRequested("save failed");
    }
}

void Files::openImage(const QImage &decoded)
{
    if (decoded.isNull())
        return;
    // 1:1 logical size, shrunk to fit and centred in the view (a little air
    // around it when it has to shrink).
    const QSizeF size = displayedSize(decoded, m_page);
    const QPointF viewCentre((m_page->width() / 2 - m_page->worldPos().x()) / m_page->zoom(),
                             (m_page->height() / 2 - m_page->worldPos().y()) / m_page->zoom());
    const QImage image = atScreenResolution(decoded, size, m_page);
    m_page->addImage(image, viewCentre - QPointF(size.width(), size.height()) / 2, size);
}

bool Files::writePng(const QRectF &worldRect, const QString &path)
{
    const QDir dir = QFileInfo(path).dir();
    if (!dir.mkpath("."))
        return false;
    return renderWorldToImage(m_page->worldItem(), worldRect, exportScale(m_page))
        .save(path, "PNG");
}

QRectF Files::exportRect() const
{
    const QRectF bounds = m_page->drawingBounds();
    return bounds.isValid()
        ? bounds.adjusted(-kExportMargin, -kExportMargin, kExportMargin, kExportMargin)
        : QRectF();
}

QString Files::defaultPath() const
{
    return QDir::home().filePath("Pictures/Drawings/"
        + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".png");
}
