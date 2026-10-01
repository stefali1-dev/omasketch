#include "selection.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <cmath>
#include <numbers>

#include "palette.h"

namespace {

QSGGeometryNode *flatNode(const QColor &color)
{
    auto *node = new QSGGeometryNode;
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);

    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    return node;
}

void addQuad(QSGGeometry::Point2D *v, int &n, const QRectF &r)
{
    v[n++].set(r.left(), r.top());
    v[n++].set(r.right(), r.top());
    v[n++].set(r.left(), r.bottom());
    v[n++].set(r.right(), r.top());
    v[n++].set(r.right(), r.bottom());
    v[n++].set(r.left(), r.bottom());
}

void addDisc(QSGGeometry::Point2D *v, int &n, const QPointF &centre, qreal radius)
{
    constexpr int kSectors = 16;
    for (int i = 0; i < kSectors; ++i) {
        const qreal a0 = 2 * std::numbers::pi_v<qreal> * i / kSectors;
        const qreal a1 = 2 * std::numbers::pi_v<qreal> * (i + 1) / kSectors;
        v[n++].set(centre.x(), centre.y());
        v[n++].set(centre.x() + radius * std::cos(a0), centre.y() + radius * std::sin(a0));
        v[n++].set(centre.x() + radius * std::cos(a1), centre.y() + radius * std::sin(a1));
    }
}

} // namespace

SelectionOverlay::SelectionOverlay(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void SelectionOverlay::setRect(const QRectF &rect)
{
    if (rect == m_rect)
        return;
    m_rect = rect;
    setPosition(rect.topLeft());
    setSize(rect.size());
    update();
}

void SelectionOverlay::appear()
{
    killFade();
    m_fade = new QVariantAnimation(this);
    m_fade->setDuration(80);
    m_fade->setStartValue(opacity());
    m_fade->setEndValue(1.0);
    connect(m_fade, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) { setOpacity(value.toReal()); });
    m_fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void SelectionOverlay::killFade()
{
    delete m_fade;
    m_fade = nullptr;
}

QSGNode *SelectionOverlay::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Runs in the scene graph sync phase; the geometry is a few hundred
    // vertices, rebuilt on every rect change is fine.
    auto *root = static_cast<QSGNode *>(oldNode);
    if (!root) {
        root = new QSGNode;
        const QColor faint(palette::accent.red(), palette::accent.green(),
                           palette::accent.blue(), 25);
        root->appendChildNode(flatNode(faint));
        root->appendChildNode(flatNode(palette::accent));
    }
    auto *fill = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
    auto *line = static_cast<QSGGeometryNode *>(root->childAtIndex(1));

    const qreal w = width();
    const qreal h = height();
    const QRectF r(0, 0, w, h);

    // The faint fill belongs to the marquee only; the selection is outline
    // plus handles.
    fill->geometry()->allocate(m_handles ? 0 : 6);
    if (!m_handles) {
        int n = 0;
        addQuad(fill->geometry()->vertexDataAsPoint2D(), n,
                r.adjusted(1, 1, -1, -1));
    }
    fill->markDirty(QSGNode::DirtyGeometry);

    const int quads = 24; // four 1 px edge strips
    const int verts = quads + (m_handles ? 4 * 16 * 3 : 0);
    line->geometry()->allocate(verts);
    QSGGeometry::Point2D *v = line->geometry()->vertexDataAsPoint2D();
    int n = 0;
    addQuad(v, n, QRectF(0, 0, w, 1));         // top
    addQuad(v, n, QRectF(0, h - 1, w, 1));     // bottom
    addQuad(v, n, QRectF(0, 0, 1, h));         // left
    addQuad(v, n, QRectF(w - 1, 0, 1, h));     // right
    if (m_handles) {
        addDisc(v, n, QPointF(0, 0), kHandleRadius);
        addDisc(v, n, QPointF(w, 0), kHandleRadius);
        addDisc(v, n, QPointF(w, h), kHandleRadius);
        addDisc(v, n, QPointF(0, h), kHandleRadius);
    }
    line->markDirty(QSGNode::DirtyGeometry);
    return root;
}
