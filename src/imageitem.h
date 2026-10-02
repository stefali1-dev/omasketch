#pragma once

#include <QImage>

#include "pageitem.h"

// A loaded PNG on the page (opened, pasted or dropped): the decoded image is
// uploaded as one texture and drawn at the item's size, so panning, zooming
// and moving never touch it. Like a stroke, a resize drag paints through the
// visualScale transform and the size is baked in once, on release.
class ImageItem : public PageItem
{
    Q_OBJECT

public:
    explicit ImageItem(QQuickItem *parent = nullptr);

    void setImage(const QImage &image, const QSizeF &size);

    const QImage &image() const { return m_image; } // tests

    // The ink is the whole rect: a click inside grabs it, nothing outside.
    bool hitTest(const QPointF &worldPos, qreal tolerance) const override;
    void scaleGeometry(qreal sx, qreal sy) override;

    QColor color() const override { return QColor(); } // images don't recolour
    void setColor(const QColor &color) override { Q_UNUSED(color); }
    bool recolourable() const override { return false; }

protected:
    QRectF localBounds() const override { return QRectF(0, 0, width(), height()); }
    QRectF boundingRect() const override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    QImage m_image;
};
