#include "tools.h"

#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtMath>

#include "page.h"
#include "palette.h"

namespace {

// Hyprland passes most Super combos on as Ctrl, but a few arrive as Super.
// Treat both the same: the tool keys only fire without either modifier.
bool plain(const QKeyEvent *event)
{
    return !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
}

// A round pixmap cursor: filled for the pencil dot, a hollow ring for the
// eraser. Sizes are logical pixels; the pixmap follows the window's scale.
QCursor circleCursor(qreal dpr, qreal diameter, const QColor &ring, const QColor &fill)
{
    const int side = qCeil((diameter + 2) * dpr);
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(dpr, dpr);
    painter.setPen(QPen(ring, 1.5));
    if (fill.isValid())
        painter.setBrush(fill);
    const qreal centre = side / (2 * dpr);
    painter.drawEllipse(QPointF(centre, centre), diameter / 2, diameter / 2);
    painter.end();

    pixmap.setDevicePixelRatio(dpr);
    return QCursor(pixmap, side / 2, side / 2);
}

} // namespace

Tools::Tools(QObject *parent)
    : QObject(parent)
{
}

void Tools::attach(QQuickWindow *window)
{
    m_window = window;
    window->installEventFilter(this);
    applyCursor();
}

void Tools::setPage(Page *page)
{
    m_page = page;
    connect(this, &Tools::toolChanged, page, [this, page] { page->setTool(m_tool); });
    connect(this, &Tools::inkChanged, page, [this, page] { page->setInk(m_ink); });
    connect(page, &Page::panningChanged, this, &Tools::applyCursor);
    page->setTool(m_tool);
    page->setInk(m_ink);
}

Page *pageIn(QQuickWindow *window)
{
    return window->findChild<Page *>();
}

QString Tools::toolName() const
{
    switch (m_tool) {
    case Draw:   return "draw";
    case Text:   return "text";
    case Eraser: return "eraser";
    case Arrow:  return "arrow";
    case Select: break;
    }
    return "select";
}

bool Tools::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (plain(key)) {
            // While the path bar or a text box has focus, letters belong to it.
            auto *focus = m_window ? m_window->activeFocusItem() : nullptr;
            if (focus && focus->flags().testFlag(QQuickItem::ItemAcceptsInputMethod))
                return QObject::eventFilter(watched, event);
            // Holding a plain key must not retrigger the tool or Space.
            if (key->isAutoRepeat())
                break;
            if (key->key() == Qt::Key_Space) {
                setSpaceHeld(true);
                break;
            }
            switch (key->key()) {
            case Qt::Key_D:      setTool(Draw); break;
            case Qt::Key_T:      setTool(Text); break;
            case Qt::Key_V:      setTool(Select); break;
            case Qt::Key_E:      setTool(Eraser); break;
            case Qt::Key_A:      setTool(Arrow); break;
            case Qt::Key_Escape: setTool(Select); break;
            case Qt::Key_1:      setInk(Black); break;
            case Qt::Key_2:      setInk(Red); break;
            case Qt::Key_3:      setInk(Blue); break;
            default:             break;
            }
        } else {
            // Ctrl/Super shortcuts (decisions.md); repeats are wanted here.
            switch (key->key()) {
            case Qt::Key_S:
                if (key->modifiers() & Qt::ShiftModifier)
                    emit pathBarRequested(QStringLiteral("save"));
                break;
            case Qt::Key_O:
                emit pathBarRequested(QStringLiteral("open"));
                break;
            case Qt::Key_Z:
                if (m_page) {
                    if (key->modifiers() & Qt::ShiftModifier)
                        m_page->redo();
                    else
                        m_page->undo();
                }
                break;
            case Qt::Key_N:     if (m_page) m_page->newPage();    break;
            case Qt::Key_Equal:
            case Qt::Key_Plus:  if (m_page) m_page->zoomStep(1);  break;
            case Qt::Key_Minus: if (m_page) m_page->zoomStep(-1); break;
            case Qt::Key_0:     if (m_page) m_page->zoomHome();   break;
            default:            break;
            }
        }
        break;
    }
    case QEvent::KeyRelease: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space && m_spaceHeld && !key->isAutoRepeat())
            setSpaceHeld(false);
        break;
    }
    case QEvent::FocusOut:
    case QEvent::WindowDeactivate:
        if (m_spaceHeld)
            setSpaceHeld(false);
        break;
    // Qt Quick delivers touchpad pinch only to gesture-aware item types, so
    // the window filter hands the event to the page itself.
    case QEvent::NativeGesture:
        if (m_page)
            m_page->pinch(static_cast<QNativeGestureEvent *>(event));
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void Tools::setSpaceHeld(bool held)
{
    m_spaceHeld = held;
    if (m_page)
        m_page->setSpaceHeld(held);
    applyCursor();
}

void Tools::setTool(Tool tool)
{
    if (m_tool == tool)
        return;
    m_tool = tool;
    applyCursor();
    emit toolChanged();
}

void Tools::setInk(Ink ink)
{
    if (m_ink == ink)
        return;
    m_ink = ink;
    emit inkChanged();
}

void Tools::applyCursor()
{
    if (!m_window)
        return;
    if (m_page && m_page->isPanning()) {
        m_window->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_spaceHeld && !(m_page && m_page->isDrawing())) {
        m_window->setCursor(Qt::OpenHandCursor);
        return;
    }
    const qreal dpr = m_window->devicePixelRatio();
    switch (m_tool) {
    case Draw:
        m_window->setCursor(circleCursor(dpr, 10, palette::page, palette::ink));
        break;
    case Eraser:
        m_window->setCursor(circleCursor(dpr, 20, palette::ui, QColor()));
        break;
    case Text:
        m_window->setCursor(Qt::IBeamCursor);
        break;
    case Arrow:
        m_window->setCursor(Qt::CrossCursor);
        break;
    case Select:
        m_window->setCursor(Qt::ArrowCursor);
        break;
    }
}
