#include "imageitem.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QVector3D>

ImageItem::ImageItem(QQuickItem *parent)
    : PageItem(parent)
{
    setFlag(ItemHasContents);
}

void ImageItem::setImage(const QImage &image, const QSizeF &size)
{
    m_image = image;
    setSize(size);
    update();
}

bool ImageItem::hitTest(const QPointF &worldPos, qreal tolerance) const
{
    Q_UNUSED(tolerance);
    return bounds().contains(worldPos);
}

void ImageItem::scaleGeometry(qreal sx, qreal sy)
{
    setSize(QSizeF(width() * sx, height() * sy));
    update();
}

QRectF ImageItem::boundingRect() const
{
    // Covers the live drag scale, so the scene graph culls the whole item.
    return scaledBounds();
}

QSGNode *ImageItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Runs in the scene graph sync phase, where the GUI thread is blocked.
    // The root node carries the live resize scale (identity outside a drag);
    // the texture node below it is created once per node lifetime.
    auto *root = static_cast<QSGTransformNode *>(oldNode);
    if (!root) {
        root = new QSGTransformNode;
        auto *node = new QSGSimpleTextureNode;
        node->setTexture(window()->createTextureFromImage(m_image));
        node->setOwnsTexture(true);
        root->appendChildNode(node);
    }
    auto *node = static_cast<QSGSimpleTextureNode *>(root->childAtIndex(0));
    node->setRect(QRectF(0, 0, width(), height()));
    if (visualScale() == QPointF(1, 1)) {
        root->setMatrix(QMatrix4x4());
    } else {
        const QPointF s = visualScale();
        QMatrix4x4 matrix;
        matrix.scale(QVector3D(float(s.x()), float(s.y()), 1));
        root->setMatrix(matrix);
    }
    return root;
}
