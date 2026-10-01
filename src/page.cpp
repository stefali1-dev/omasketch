#include "page.h"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QtMath>

#include "palette.h"
#include "stroke.h"

namespace {

constexpr qreal kZoomMin = 0.1;
constexpr qreal kZoomMax = 8.0;
constexpr qreal kZoomStep = 1.25; // one Ctrl+= press, one Super+scroll notch
constexpr int kStepEase = 120;    // ms per zoom step
constexpr int kHomeEase = 150;    // ms back to the drawing

} // namespace

// Owns its stroke for as long as it sits on the stack; the page adds it to and
// removes it from the world as the stack moves through time.
class AddStroke : public QUndoCommand
{
public:
    AddStroke(Page *page, Stroke *stroke)
        : m_page(page), m_stroke(stroke)
    {
        setText("draw");
    }

    ~AddStroke() override { delete m_stroke; }

    void undo() override { m_page->removeStroke(m_stroke); }
    void redo() override { m_page->addStroke(m_stroke); }

private:
    Page *m_page;
    Stroke *m_stroke;
};

// Does not own the strokes; their AddStroke commands keep them alive while
// they are off the world.
class ClearPage : public QUndoCommand
{
public:
    ClearPage(Page *page, const QList<Stroke *> &strokes)
        : m_page(page), m_strokes(strokes)
    {
        setText("new page");
    }

    void undo() override
    {
        for (Stroke *stroke : m_strokes)
            m_page->addStroke(stroke);
    }

    void redo() override
    {
        for (Stroke *stroke : m_strokes)
            m_page->removeStroke(stroke);
    }

private:
    Page *m_page;
    QList<Stroke *> m_strokes;
};

Page::Page(QQuickItem *parent)
    : QQuickItem(parent)
    , m_world(new QQuickItem(this))
    , m_undo(new QUndoStack(this))
{
    // Scale and position are applied around the world's top-left corner so the
    // anchor math below stays in one place.
    m_world->setTransformOrigin(QQuickItem::TopLeft);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

Page::~Page()
{
    // Commands own their strokes; drop the stack before the item tree goes.
    delete m_undo;
}

void Page::setTool(Tools::Tool tool)
{
    m_tool = tool;
}

void Page::setInk(Tools::Ink ink)
{
    switch (ink) {
    case Tools::Black: m_ink = palette::ink;  break;
    case Tools::Red:   m_ink = palette::red;  break;
    case Tools::Blue:  m_ink = palette::blue; break;
    }
}

void Page::undo()
{
    if (!m_stroke)
        m_undo->undo();
}

void Page::redo()
{
    if (!m_stroke)
        m_undo->redo();
}

void Page::newPage()
{
    if (m_stroke || m_strokes.isEmpty())
        return;
    m_undo->push(new ClearPage(this, m_strokes));
}

void Page::zoomStep(int direction)
{
    if (m_stroke || m_panning)
        return;
    const qreal target = m_zoom * qPow(kZoomStep, direction);
    animateTo(target, posForZoom(target, anchorPoint()), kStepEase);
}

void Page::zoomHome()
{
    if (m_stroke || m_panning)
        return;
    const QRectF bounds = drawingBounds();
    const QPointF target = bounds.isValid()
        ? QPointF(width() / 2, height() / 2) - bounds.center() // drawing centred
        : QPointF(0, 0);                                       // the start position
    animateTo(1.0, target, kHomeEase);
}

void Page::setSpaceHeld(bool held)
{
    // A press mid-stroke still lands here; the mouse handlers decide that no
    // pan can start until the stroke is done.
    m_spaceHeld = held;
}

void Page::pinch(QNativeGestureEvent *event)
{
    if (event->gestureType() != Qt::ZoomNativeGesture || m_stroke || m_panning)
        return;
    setZoomAt(m_zoom * (1 + event->value()), mapFromScene(event->scenePosition()));
}

void Page::addStroke(Stroke *stroke)
{
    if (!m_strokes.contains(stroke))
        m_strokes.append(stroke);
    stroke->setParentItem(m_world);
}

void Page::removeStroke(Stroke *stroke)
{
    m_strokes.removeOne(stroke);
    stroke->setParentItem(nullptr);
}

QRectF Page::drawingBounds() const
{
    QRectF bounds;
    for (const Stroke *stroke : m_strokes) {
        if (bounds.isValid())
            bounds = bounds.united(stroke->bounds());
        else
            bounds = stroke->bounds();
    }
    return bounds;
}

void Page::hoverMoveEvent(QHoverEvent *event)
{
    m_mouse = event->position();
    m_mouseSeen = true;
}

void Page::mousePressEvent(QMouseEvent *event)
{
    m_mouse = event->position();
    m_mouseSeen = true;
    if (event->button() != Qt::LeftButton || m_stroke)
        return;
    if (m_spaceHeld) {
        m_panning = true;
        m_panGrab = event->position();
        m_panStart = m_world->position();
        emit panningChanged();
        event->accept();
        return;
    }
    if (m_tool != Tools::Draw)
        return;
    m_stroke = new Stroke;
    m_stroke->setColor(m_ink);
    m_stroke->setStrokeWidth(m_strokeWidth);
    m_stroke->setParentItem(m_world); // visible while drawing
    m_stroke->begin(toWorld(event->position()));
    event->accept();
}

void Page::mouseMoveEvent(QMouseEvent *event)
{
    m_mouse = event->position();
    m_mouseSeen = true;
    if (m_panning) {
        m_world->setPosition(m_panStart + event->position() - m_panGrab);
        event->accept();
        return;
    }
    if (m_stroke) {
        m_stroke->addPoint(toWorld(event->position()));
        event->accept();
    }
}

void Page::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_panning) {
        m_panning = false;
        emit panningChanged();
        event->accept();
        return;
    }
    if (m_stroke) {
        m_stroke->addPoint(toWorld(event->position()));
        m_stroke->end();
        m_undo->push(new AddStroke(this, m_stroke)); // redo re-adds it: a no-op
        m_stroke = nullptr;
        event->accept();
    }
}

void Page::wheelEvent(QWheelEvent *event)
{
    if (m_stroke || m_panning)
        return;
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & (Qt::ControlModifier | Qt::MetaModifier)) {
        // Super+scroll arrives as Ctrl+wheel, one 120-notch per step.
        setZoomAt(m_zoom * qPow(kZoomStep, event->angleDelta().y() / 120.0),
                  event->position());
        event->accept();
        return;
    }
    QPointF delta = event->pixelDelta();
    if (delta.isNull())
        delta = QPointF(event->angleDelta()); // one wheel notch (120) pans 120 px
    const QPointF pan = mods & Qt::ShiftModifier
        ? QPointF(delta.x() != 0 ? delta.x() : delta.y(), 0)
        : delta;
    if (!pan.isNull()) {
        killAnim();
        m_world->setPosition(m_world->position() + pan);
    }
    event->accept();
}

QPointF Page::toWorld(const QPointF &pagePos) const
{
    return (pagePos - m_world->position()) / m_zoom;
}

QPointF Page::anchorPoint() const
{
    return m_mouseSeen ? m_mouse : QPointF(width() / 2, height() / 2);
}

QPointF Page::posForZoom(qreal zoom, const QPointF &anchor) const
{
    // Keep the world point under the anchor on screen: anchor = pos + world*zoom.
    const QPointF world = (anchor - m_world->position()) / m_zoom;
    return anchor - world * zoom;
}

void Page::setZoomAt(qreal zoom, const QPointF &anchor)
{
    killAnim();
    zoom = qBound(kZoomMin, zoom, kZoomMax);
    const QPointF pos = posForZoom(zoom, anchor); // reads the current zoom
    m_zoom = zoom;
    m_world->setScale(zoom);
    m_world->setPosition(pos);
}

void Page::animateTo(qreal zoom, const QPointF &pos, int duration)
{
    killAnim();
    zoom = qBound(kZoomMin, zoom, kZoomMax);
    const qreal z0 = m_zoom;
    const QPointF p0 = m_world->position();
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(duration);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this,
            [this, z0, p0, zoom, pos](const QVariant &t) {
                const qreal f = t.toReal();
                m_zoom = z0 * qPow(zoom / z0, f); // interpolate zoom geometrically
                m_world->setScale(m_zoom);
                m_world->setPosition(p0 + (pos - p0) * f);
            });
    m_anim->start();
}

void Page::killAnim()
{
    delete m_anim;
    m_anim = nullptr;
}
