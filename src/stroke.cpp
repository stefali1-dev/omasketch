#include "stroke.h"

#include <QMatrix4x4>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QVector3D>
#include <cmath>

// Antialiasing comes from the window's multisampled surface (main.cpp), so the
// strip is plain opaque geometry: two vertices per path point, offset by half
// the stroke width along the path normal, with straight end caps.
//
// The path is smoothed with quadratic Béziers through the midpoints between
// raw input points, the standard zero-lag filter: the drawn end always lands
// exactly on the newest pointer position. Béziers stay inside the control
// polygon, so the cached bounds from the raw points cover the whole stroke.

namespace {

qreal distanceSquared(const QPointF &a, const QPointF &b)
{
    const QPointF d = b - a;
    return d.x() * d.x() + d.y() * d.y();
}

// Squared distance from p to the segment a-b.
qreal segmentDistanceSquared(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal length2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (length2 == 0)
        return distanceSquared(p, a);
    const qreal t = qBound(0.0, QPointF::dotProduct(p - a, ab) / length2, 1.0);
    return distanceSquared(p, a + ab * t);
}

// Whether a segment touches a rect (Liang-Barsky clip): true when an endpoint
// is inside or the segment crosses any of the four edges.
bool segmentTouchesRect(const QPointF &a, const QPointF &b, const QRectF &r)
{
    if (r.contains(a) || r.contains(b))
        return true;
    const qreal dx = b.x() - a.x();
    const qreal dy = b.y() - a.y();
    const qreal p[] = {-dx, dx, -dy, dy};
    const qreal q[] = {a.x() - r.left(), r.right() - a.x(),
                       a.y() - r.top(),  r.bottom() - a.y()};
    qreal t0 = 0;
    qreal t1 = 1;
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return false; // parallel to this pair of edges and outside
        } else {
            const qreal t = q[i] / p[i];
            if (p[i] < 0) {
                if (t > t1) return false;
                if (t > t0) t0 = t;
            } else {
                if (t < t0) return false;
                if (t < t1) t1 = t;
            }
        }
    }
    return true;
}

} // namespace

Stroke::Stroke(QQuickItem *parent)
    : PageItem(parent)
{
    setFlag(ItemHasContents);
}

void Stroke::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    // The material carries the colour; updatePaintNode applies it on the next
    // render (a detached item picks it up when it is re-added).
    m_colorDirty = true;
    update();
}

void Stroke::begin(const QPointF &point)
{
    // While drawing the item sits at 0,0 in the world, so world and item
    // coordinates coincide.
    m_points.append(point);
    m_geomBounds = QRectF(point.x() - m_width, point.y() - m_width,
                          2 * m_width, 2 * m_width);
    m_geometryDirty = true;
    update();
}

void Stroke::addPoint(const QPointF &point)
{
    // Sub-pixel jitter only fattens the point list without changing the line.
    if (!m_points.isEmpty() && (point - m_points.constLast()).manhattanLength() < 0.5)
        return;
    m_points.append(point);
    if (!m_geomBounds.contains(point))
        m_geomBounds = m_geomBounds.united(QRectF(point.x() - m_width, point.y() - m_width,
                                                  2 * m_width, 2 * m_width));
    m_geometryDirty = true;
    update();
}

void Stroke::recomputeBounds()
{
    if (m_points.isEmpty()) {
        m_geomBounds = QRectF();
        return;
    }
    QPointF lo = m_points.constFirst();
    QPointF hi = lo;
    for (const QPointF &p : m_points) {
        lo.setX(qMin(lo.x(), p.x()));
        lo.setY(qMin(lo.y(), p.y()));
        hi.setX(qMax(hi.x(), p.x()));
        hi.setY(qMax(hi.y(), p.y()));
    }
    m_geomBounds = QRectF(lo - QPointF(m_width, m_width), hi + QPointF(m_width, m_width));
}

bool Stroke::hitTest(const QPointF &worldPos, qreal tolerance) const
{
    // Bounds reject first: this runs over every item on the page.
    if (!bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(worldPos))
        return false;
    const QPointF p = worldPos - position(); // to item coordinates
    const qreal tolerance2 = tolerance * tolerance;
    if (m_points.size() == 1)
        return distanceSquared(p, m_points.constFirst()) <= tolerance2;
    for (int i = 0; i + 1 < m_points.size(); ++i) {
        if (segmentDistanceSquared(p, m_points[i], m_points[i + 1]) <= tolerance2)
            return true;
    }
    return false;
}

bool Stroke::touchesRect(const QRectF &worldRect) const
{
    // The rect must touch the ink, not the bounding box: grow it by half the
    // line width and test every segment against it.
    const QRectF rect = worldRect.adjusted(-m_width / 2, -m_width / 2,
                                           m_width / 2, m_width / 2)
                                   .translated(-position());
    if (!rect.intersects(m_geomBounds))
        return false;
    if (m_points.size() == 1)
        return rect.contains(m_points.constFirst());
    for (int i = 0; i + 1 < m_points.size(); ++i) {
        if (segmentTouchesRect(m_points[i], m_points[i + 1], rect))
            return true;
    }
    return false;
}

void Stroke::scaleGeometry(qreal sx, qreal sy)
{
    for (QPointF &p : m_points) {
        p.setX(p.x() * sx);
        p.setY(p.y() * sy);
    }
    recomputeBounds();
    m_geometryDirty = true;
    update();
}

QRectF Stroke::boundingRect() const
{
    // Covers the geometry, live scale included, so the scene graph culls
    // whole strokes, never parts of them.
    return scaledBounds();
}

QList<QPointF> Stroke::smoothedPath() const
{
    const int n = m_points.size();
    QList<QPointF> path;
    if (n == 0)
        return path;
    path.reserve(n * 2);
    path.append(m_points.constFirst());

    // Four subdivisions per quadratic keep faceting far below a pixel.
    constexpr int kSegments = 4;
    for (int i = 1; i < n - 1; ++i) {
        const QPointF from = (m_points[i - 1] + m_points[i]) / 2;
        const QPointF to = (m_points[i] + m_points[i + 1]) / 2;
        for (int s = 1; s <= kSegments; ++s) {
            const qreal t = qreal(s) / kSegments;
            const qreal u = 1 - t;
            path.append(u * u * from + 2 * u * t * m_points[i] + t * t * to);
        }
    }
    if (n > 1)
        path.append(m_points.constLast());
    return path;
}

QSGNode *Stroke::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Runs in the scene graph sync phase, where the GUI thread is blocked, so
    // reading m_points here is safe without locking. The root node carries
    // the live resize scale (identity outside a drag); the geometry node
    // below it is the same as ever.
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
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
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
    ++m_buildCount;

    const QList<QPointF> path = smoothedPath();
    const qreal half = m_width / 2;

    QSGGeometry *geometry = node->geometry();
    if (path.size() == 1) {
        // A tap: a small square the width of the stroke.
        geometry->allocate(4);
        QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
        v[0].set(path[0].x() - half, path[0].y() - half);
        v[1].set(path[0].x() + half, path[0].y() - half);
        v[2].set(path[0].x() - half, path[0].y() + half);
        v[3].set(path[0].x() + half, path[0].y() + half);
        node->markDirty(QSGNode::DirtyGeometry);
        return root;
    }

    geometry->allocate(int(path.size()) * 2);
    QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
    QPointF normal(0, half); // reused unchanged when consecutive points repeat
    for (int i = 0; i < path.size(); ++i) {
        const QPointF dir = path[qMin(i + 1, path.size() - 1)] - path[qMax(i - 1, 0)];
        const qreal len = std::hypot(dir.x(), dir.y());
        if (len > 0.01)
            normal = QPointF(-dir.y(), dir.x()) * (half / len);
        v[i * 2].set(path[i].x() + normal.x(), path[i].y() + normal.y());
        v[i * 2 + 1].set(path[i].x() - normal.x(), path[i].y() - normal.y());
    }
    node->markDirty(QSGNode::DirtyGeometry);
    return root;
}
