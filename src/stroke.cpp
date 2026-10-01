#include "stroke.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <cmath>

// Antialiasing comes from the window's multisampled surface (main.cpp), so the
// strip is plain opaque geometry: two vertices per path point, offset by half
// the stroke width along the path normal, with straight end caps.
//
// The path is smoothed with quadratic Béziers through the midpoints between
// raw input points, the standard zero-lag filter: the drawn end always lands
// exactly on the newest pointer position. Béziers stay inside the control
// polygon, so bounds() from the raw points covers the whole stroke.

Stroke::Stroke(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void Stroke::begin(const QPointF &point)
{
    m_points.append(point);
    m_geometryDirty = true;
    update();
}

void Stroke::addPoint(const QPointF &point)
{
    // Sub-pixel jitter only fattens the point list without changing the line.
    if (!m_points.isEmpty() && (point - m_points.constLast()).manhattanLength() < 0.5)
        return;
    m_points.append(point);
    m_geometryDirty = true;
    update();
}

QRectF Stroke::bounds() const
{
    if (m_points.isEmpty())
        return {};
    QPointF lo = m_points.constFirst();
    QPointF hi = lo;
    for (const QPointF &p : m_points) {
        lo.setX(qMin(lo.x(), p.x()));
        lo.setY(qMin(lo.y(), p.y()));
        hi.setX(qMax(hi.x(), p.x()));
        hi.setY(qMax(hi.y(), p.y()));
    }
    return QRectF(lo - QPointF(m_width, m_width), hi + QPointF(m_width, m_width));
}

QRectF Stroke::boundingRect() const
{
    // Covers the geometry so the scene graph culls whole strokes, never parts
    // of them.
    return bounds();
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
    // reading m_points here is safe without locking.
    auto *node = static_cast<QSGGeometryNode *>(oldNode);
    // Undoing removes the item and redoing re-adds it, dropping the old node;
    // a fresh node needs the geometry even though nothing changed.
    const bool rebuild = m_geometryDirty || node == nullptr;
    if (!node) {
        node = new QSGGeometryNode;
        auto *material = new QSGFlatColorMaterial;
        material->setColor(m_color);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);

        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
    }
    if (!rebuild)
        return node;

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
        return node;
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
    return node;
}
