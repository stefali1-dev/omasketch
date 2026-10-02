#pragma once

#include <QQuickItem>

// Base for everything on the page (a stroke today; text, arrows and images
// later). Geometry lives in item coordinates; the item's position places it
// in the world. Moving changes only the position, so nothing is rebuilt; a
// resize drag sets the position and a live scale (applied by the item's paint
// node as a transform), and the geometry itself is rescaled once, on release.
class PageItem : public QQuickItem
{
    Q_OBJECT

public:
    explicit PageItem(QQuickItem *parent = nullptr)
        : QQuickItem(parent)
    {
    }

    // The live scale of a resize drag; the item's painted node applies it as
    // a transform, so nothing rebuilds per frame. Identity outside a drag.
    QPointF visualScale() const { return m_visualScale; }
    void setVisualScale(const QPointF &scale)
    {
        if (m_visualScale == scale)
            return;
        m_visualScale = scale;
        update();
    }

    // Bounds in world coordinates: the local bounds, scaled and offset by
    // the position.
    QRectF bounds() const { return scaledBounds().translated(position()); }

    // True when the world point is within tolerance (world units) of the ink.
    virtual bool hitTest(const QPointF &worldPos, qreal tolerance) const = 0;

    // True when the world rect touches the ink; the default tests the bounds.
    virtual bool touchesRect(const QRectF &worldRect) const
    {
        return bounds().intersects(worldRect);
    }

    virtual QColor color() const = 0;
    virtual void setColor(const QColor &color) = 0;

    // Whether 1/2/3 recolour it; images say no and the page's recolour
    // skips them.
    virtual bool recolourable() const { return true; }

    // Multiplies the local geometry by (sx, sy) about the item's origin and
    // rebuilds it once. Never call this per frame: a drag sets visualScale
    // instead.
    virtual void scaleGeometry(qreal sx, qreal sy) = 0;

protected:
    virtual QRectF localBounds() const = 0;

    // localBounds() with the live drag scale applied (flips normalized).
    QRectF scaledBounds() const
    {
        const QRectF &local = localBounds();
        return QRectF(m_visualScale.x() * local.left(), m_visualScale.y() * local.top(),
                      m_visualScale.x() * local.width(),
                      m_visualScale.y() * local.height()).normalized();
    }

private:
    QPointF m_visualScale = QPointF(1, 1);
};
