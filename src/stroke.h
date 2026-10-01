#pragma once

#include <QColor>
#include <QList>
#include <QRectF>

#include "pageitem.h"

// One freehand stroke: a polyline ribbon drawn as a triangle strip. Geometry
// lives in item coordinates (the item sits at 0,0 in the world until a move
// gives it a position), so panning and zooming transform it without ever
// touching the geometry. While the stroke is being drawn the geometry is
// rebuilt as points arrive; once finished it is rebuilt only when the
// selection around it is resized (once, on release).
class Stroke : public PageItem
{
    Q_OBJECT

public:
    explicit Stroke(QQuickItem *parent = nullptr);

    QColor color() const override { return m_color; }
    void setColor(const QColor &color) override;
    void setStrokeWidth(qreal width) { m_width = width; }

    void begin(const QPointF &point);
    void addPoint(const QPointF &point);

    bool hitTest(const QPointF &worldPos, qreal tolerance) const override;
    bool touchesRect(const QRectF &worldRect) const override;
    void scaleGeometry(qreal sx, qreal sy) override;

    // Times the geometry was (re)built; lets the tests and bench assert that
    // panning, zooming and moving rebuild nothing.
    int geometryBuildCount() const { return m_buildCount; }

protected:
    QRectF localBounds() const override { return m_geomBounds; }
    QRectF boundingRect() const override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
private:
    QList<QPointF> smoothedPath() const;
    void recomputeBounds();

    QColor m_color = Qt::black;
    qreal m_width = 3;
    QList<QPointF> m_points;
    QRectF m_geomBounds; // local bounds, width included; kept incrementally
    bool m_geometryDirty = false;
    bool m_colorDirty = false;
    int m_buildCount = 0;
};
