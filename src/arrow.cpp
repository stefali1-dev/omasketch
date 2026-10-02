#include "arrow.h"

#include <QMatrix4x4>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QVector3D>
#include <QtMath>
#include <cmath>

#include "geometry.h"

// Antialiasing comes from the window's multisampled surface (main.cpp), so
// the arrow is plain opaque geometry: the shaft is a quad from the start to
// the head's base, the head a triangle on that base. They are triangles in
// one geometry sharing the base edge, all one colour, so there is no seam
// between line and head. The head is sized in world units — 5.5 stroke
// widths long, 4 wide — and scales with zoom exactly like the shaft does.

namespace {

constexpr qreal kHeadLengths = 5.5;  // head length, in stroke widths
constexpr qreal kHeadHalfWidths = 2; // half the head base, in stroke widths

} // namespace

Arrow::Arrow(QQuickItem *parent)
    : PageItem(parent)
{
    setFlag(ItemHasContents);
}

void Arrow::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    // The material carries the colour; updatePaintNode applies it on the next
    // render (a detached item picks it up when it is re-added).
    m_colorDirty = true;
    update();
}

void Arrow::begin(const QPointF &start)
{
    // While drawing the item sits at 0,0 in the world, so world and item
    // coordinates coincide.
    m_start = start;
    m_end = start;
    recomputeBounds();
    m_geometryDirty = true;
    update();
}

void Arrow::setEnd(const QPointF &end)
{
    if (m_end == end)
        return;
    m_end = end;
    recomputeBounds();
    m_geometryDirty = true;
    update();
}

QPointF Arrow::snapped(const QPointF &end) const
{
    const QPointF d = end - m_start;
    const qreal length = std::hypot(d.x(), d.y());
    if (length == 0)
        return end;
    const qreal step = M_PI / 4;
    const qreal angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
    return m_start + QPointF(std::cos(angle), std::sin(angle)) * length;
}

void Arrow::recomputeBounds()
{
    // The head's corners sit half a base width off the line; nothing else is
    // farther out.
    const qreal margin = kHeadHalfWidths * m_width;
    m_geomBounds = QRectF(m_start, m_end).normalized()
                       .adjusted(-margin, -margin, margin, margin);
}

QPolygonF Arrow::head() const
{
    const QPointF line = m_end - m_start;
    const qreal length = std::hypot(line.x(), line.y());
    const QPointF dir = length > 0 ? line / length : QPointF(1, 0);
    const qreal headLength = qMin(kHeadLengths * m_width, length);
    const QPointF base = m_end - dir * headLength;
    const QPointF out = QPointF(-dir.y(), dir.x()) * (kHeadHalfWidths * m_width);
    return {base + out, base - out, m_end};
}

bool Arrow::hitTest(const QPointF &worldPos, qreal tolerance) const
{
    // Bounds reject first: this runs over every item on the page.
    if (!bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(worldPos))
        return false;
    const QPointF p = worldPos - position(); // to item coordinates
    if (geom::segmentDistanceSquared(p, m_start, m_end) <= tolerance * tolerance)
        return true;
    return head().containsPoint(p, Qt::OddEvenFill);
}

bool Arrow::touchesRect(const QRectF &worldRect) const
{
    // The rect must touch the ink, not the bounding box: the shaft as a
    // segment grown by half the line width, the head as a triangle.
    const QRectF rect = worldRect.translated(-position());
    if (!rect.intersects(m_geomBounds))
        return false;
    const qreal half = m_width / 2;
    if (geom::segmentTouchesRect(m_start, m_end,
                                 rect.adjusted(-half, -half, half, half)))
        return true;
    const QPolygonF tri = head();
    for (const QPointF &corner : tri) {
        if (rect.contains(corner))
            return true;
    }
    for (const QPointF &corner : {rect.topLeft(), rect.topRight(),
                                  rect.bottomRight(), rect.bottomLeft()}) {
        if (tri.containsPoint(corner, Qt::OddEvenFill))
            return true;
    }
    return geom::segmentTouchesRect(tri[0], tri[1], rect)
        || geom::segmentTouchesRect(tri[1], tri[2], rect)
        || geom::segmentTouchesRect(tri[2], tri[0], rect);
}

void Arrow::scaleGeometry(qreal sx, qreal sy)
{
    m_start = QPointF(m_start.x() * sx, m_start.y() * sy);
    m_end = QPointF(m_end.x() * sx, m_end.y() * sy);
    recomputeBounds();
    m_geometryDirty = true;
    update();
}

QRectF Arrow::boundingRect() const
{
    // Covers the geometry, live scale included, so the scene graph culls
    // whole arrows, never parts of them.
    return scaledBounds();
}

QSGNode *Arrow::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Runs in the scene graph sync phase, where the GUI thread is blocked, so
    // reading the endpoints here is safe without locking. The root node
    // carries the live resize scale (identity outside a drag); the geometry
    // node below it is the same as ever.
    auto *root = static_cast<QSGTransformNode *>(oldNode);
    bool fresh = false;
    if (!root) {
        root = new QSGTransformNode;
        auto *node = new QSGGeometryNode;
        auto *material = new QSGFlatColorMaterial;
        material->setColor(m_color);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);

        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        root->appendChildNode(node);
        fresh = true;
    }
    auto *node = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
    // Undoing removes the item and redoing re-adds it, dropping the old node;
    // a fresh node needs the geometry even though nothing changed.
    const bool rebuild = m_geometryDirty || fresh;
    if (m_colorDirty) {
        static_cast<QSGFlatColorMaterial *>(node->material())->setColor(m_color);
        node->markDirty(QSGNode::DirtyMaterial);
        m_colorDirty = false;
    }
    if (visualScale() == QPointF(1, 1)) {
        root->setMatrix(QMatrix4x4());
    } else {
        const QPointF s = visualScale();
        QMatrix4x4 matrix;
        matrix.scale(QVector3D(float(s.x()), float(s.y()), 1));
        root->setMatrix(matrix);
    }
    if (!rebuild)
        return root;

    m_geometryDirty = false;

    // The head from the one derivation hit-testing also uses; only the
    // shaft's own half-width offset is computed here.
    const QPolygonF tri = head();
    const QPointF base = (tri[0] + tri[1]) / 2;
    const QPointF headOut = (tri[0] - tri[1]) / 2;
    const QPointF line = m_end - m_start;
    const qreal length = std::hypot(line.x(), line.y());
    const QPointF across = length > 0
        ? QPointF(-line.y(), line.x()) / length : QPointF(0, 1);
    const QPointF shaftOut = across * (m_width / 2);

    QSGGeometry *geometry = node->geometry();
    geometry->allocate(9);
    QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
    // The shaft quad, as two triangles.
    v[0].set(m_start.x() + shaftOut.x(), m_start.y() + shaftOut.y());
    v[1].set(m_start.x() - shaftOut.x(), m_start.y() - shaftOut.y());
    v[2].set(base.x() + shaftOut.x(), base.y() + shaftOut.y());
    v[3].set(m_start.x() - shaftOut.x(), m_start.y() - shaftOut.y());
    v[4].set(base.x() - shaftOut.x(), base.y() - shaftOut.y());
    v[5].set(base.x() + shaftOut.x(), base.y() + shaftOut.y());
    // The head, on the shaft's end: no gap, one shared edge.
    v[6].set(base.x() + headOut.x(), base.y() + headOut.y());
    v[7].set(base.x() - headOut.x(), base.y() - headOut.y());
    v[8].set(m_end.x(), m_end.y());
    node->markDirty(QSGNode::DirtyGeometry);
    return root;
}
