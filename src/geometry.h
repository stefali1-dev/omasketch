#pragma once

#include <QPointF>
#include <QRectF>

// Segment math shared by the page items' hit tests (stroke.cpp, arrow.cpp).
namespace geom {

inline qreal distanceSquared(const QPointF &a, const QPointF &b)
{
    const QPointF d = b - a;
    return d.x() * d.x() + d.y() * d.y();
}

// Squared distance from p to the segment a-b.
inline qreal segmentDistanceSquared(const QPointF &p, const QPointF &a, const QPointF &b)
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
inline bool segmentTouchesRect(const QPointF &a, const QPointF &b, const QRectF &r)
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

} // namespace geom
