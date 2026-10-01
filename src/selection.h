#pragma once

#include <QQuickItem>
#include <QVariantAnimation>

// Radius of the round corner handles and the grab distance around one, in
// screen pixels; page.cpp hit-tests the handles with the grab distance.
constexpr qreal kHandleRadius = 4.5;
constexpr qreal kHandleGrab = 9;

// The accent rectangle the select tool draws: around the selection with four
// round corner handles, or as the marquee box while it is dragged (no
// handles, plus a very faint fill). It lives on the page itself, not in the
// world item, so panning and zooming never scale it. The page positions it in
// page coordinates; an invalid rect hides it.
class SelectionOverlay : public QQuickItem
{
    Q_OBJECT

public:
    explicit SelectionOverlay(QQuickItem *parent = nullptr);

    void setRect(const QRectF &rect);
    void setHandles(bool on) { m_handles = on; update(); }
    void appear(); // quick fade-in

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    void killFade();

    QRectF m_rect;
    bool m_handles = true;
    QVariantAnimation *m_fade = nullptr;
};
