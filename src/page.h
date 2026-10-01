#pragma once

#include <QColor>
#include <QQuickItem>
#include <QtQml/qqml.h>

#include "tools.h"

class QNativeGestureEvent;
class QUndoStack;
class QVariantAnimation;
class Stroke;

// The drawing surface: fills the window, owns the "world" item that holds
// everything drawn, and turns pointer input for the current tool into strokes,
// pans and zooms. Panning and zooming only move and scale the world item;
// stroke geometry is in world coordinates and never changes. Keys land in
// Tools' window event filter, which calls the methods below.
class Page : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool panning READ isPanning NOTIFY panningChanged)

public:
    explicit Page(QQuickItem *parent = nullptr);
    ~Page() override;

    void setTool(Tools::Tool tool);
    void setInk(Tools::Ink ink);

    void undo();
    void redo();
    void newPage();
    void zoomStep(int direction);
    void zoomHome();
    void setSpaceHeld(bool held);
    void pinch(QNativeGestureEvent *event);

    bool isPanning() const { return m_panning; }
    bool isDrawing() const { return m_stroke != nullptr; }

    // Introspection for the tests and the frame-time bench.
    qreal zoom() const { return m_zoom; }
    QPointF worldPos() const { return m_world->position(); }
    QRectF drawingBounds() const;
    QList<Stroke *> strokes() const { return m_strokes; }

    // Undo plumbing; the QUndoCommand classes in page.cpp call these.
    void addStroke(Stroke *stroke);
    void removeStroke(Stroke *stroke);

signals:
    void panningChanged();

protected:
    void hoverMoveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QPointF toWorld(const QPointF &pagePos) const;
    QPointF anchorPoint() const;
    QPointF posForZoom(qreal zoom, const QPointF &anchor) const;
    void setZoomAt(qreal zoom, const QPointF &anchor);
    void animateTo(qreal zoom, const QPointF &pos, int duration);
    void killAnim();

    QQuickItem *m_world;
    QUndoStack *m_undo;
    QList<Stroke *> m_strokes;

    Stroke *m_stroke = nullptr; // stroke in progress
    Tools::Tool m_tool = Tools::Select;
    QColor m_ink = Qt::black;
    qreal m_strokeWidth = 2.75;

    qreal m_zoom = 1.0;
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPointF m_panGrab;  // page position where the pan drag started
    QPointF m_panStart; // world position at that moment
    QPointF m_mouse;    // last cursor position in page coordinates
    bool m_mouseSeen = false;
    QVariantAnimation *m_anim = nullptr;
};
