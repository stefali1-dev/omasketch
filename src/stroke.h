#pragma once

#include <QColor>
#include <QQuickItem>

// One freehand stroke: a polyline ribbon drawn as a triangle strip. Geometry
// lives in world coordinates (the item sits at 0,0 in the world item), so
// panning and zooming transform it without ever touching the geometry.
// While the stroke is being drawn the geometry is rebuilt as points arrive;
// once finished it is built for the last time and never changes.
class Stroke : public QQuickItem
{
    Q_OBJECT

public:
    explicit Stroke(QQuickItem *parent = nullptr);

    void setColor(const QColor &color) { m_color = color; }
    void setStrokeWidth(qreal width) { m_width = width; }

    void begin(const QPointF &point);
    void addPoint(const QPointF &point);

    // Bounds in world coordinates, width included; used for Ctrl+0 centring.
    QRectF bounds() const;

    // Times the geometry was (re)built; lets the tests and bench assert that
    // panning and zooming rebuild nothing.
    int geometryBuildCount() const { return m_buildCount; }

protected:
    QRectF boundingRect() const override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    QList<QPointF> smoothedPath() const;

    QColor m_color = Qt::black;
    qreal m_width = 3;
    QList<QPointF> m_points;
    bool m_geometryDirty = false;
    int m_buildCount = 0;
};
