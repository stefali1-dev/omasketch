#include "tools.h"

#include <QKeyEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QtMath>

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
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (plain(key)) {
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
        }
    }
    return QObject::eventFilter(watched, event);
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
