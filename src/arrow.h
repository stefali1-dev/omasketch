#pragma once

#include <QColor>
#include <QPolygonF>
#include <QRectF>

#include "pageitem.h"

// One straight arrow: a line from the start point to the end point with a
// filled triangular head at the end. Built like Stroke — plain opaque
// geometry in item coordinates, rebuilt while the pointer drags and only
// again when a selection resize is baked in — so everything the page does to
// strokes works on arrows with no special cases.
class Arrow : public PageItem
{
    Q_OBJECT

public:
    explicit Arrow(QQuickItem *parent = nullptr);

    QColor color() const override { return m_color; }
    void setColor(const QColor &color) override;
    void setStrokeWidth(qreal width) { m_width = width; }

    void begin(const QPointF &start);
    void setEnd(const QPointF &end);

    // The end point swung to the nearest 45° direction from the start, same
    // distance out: what a Shift drag means.
    QPointF snapped(const QPointF &end) const;

    bool hitTest(const QPointF &worldPos, qreal tolerance) const override;
    bool touchesRect(const QRectF &worldRect) const override;
    void scaleGeometry(qreal sx, qreal sy) override;

protected:
    QRectF localBounds() const override { return m_geomBounds; }
    QRectF boundingRect() const override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
private:
    // The head triangle in item coordinates.
    QPolygonF head() const;
    void recomputeBounds();

    QColor m_color = Qt::black;
    qreal m_width = 3;
    QPointF m_start;
    QPointF m_end;
    QRectF m_geomBounds; // local bounds, head width included
    bool m_geometryDirty = false;
    bool m_colorDirty = false;
};
