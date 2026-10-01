#pragma once

#include <QColor>
#include <QList>
#include <QQuickItem>
#include <QtQml/qqml.h>

#include "tools.h"

class QNativeGestureEvent;
class QUndoStack;
class QVariantAnimation;
class PageItem;
class SelectionOverlay;
class Stroke;

// The drawing surface: fills the window, owns the "world" item that holds
// everything drawn, and turns pointer input for the current tool into strokes,
// pans, zooms and selection drags. Panning and zooming only move and scale
// the world item; item geometry is in world coordinates and never changes
// except when a resize is baked in. Keys land in Tools' window event filter,
// which calls the methods below.
class Page : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool panning READ isPanning NOTIFY panningChanged)

public:
    explicit Page(QQuickItem *parent = nullptr);
    ~Page() override;

    void setTool(Tools::Tool tool);
    void setInk(Tools::Ink ink);

    void undo();
    void redo();
    void newPage();
    void zoomStep(int direction);
    void zoomHome();
    void setSpaceHeld(bool held);
    void pinch(QNativeGestureEvent *event);

    bool isPanning() const { return m_panning; }
    bool isDrawing() const { return m_stroke != nullptr; }

    // Introspection for the tests and the frame-time bench.
    qreal zoom() const { return m_zoom; }
    QPointF worldPos() const { return m_world->position(); }
    QRectF drawingBounds() const;
    QList<PageItem *> items() const { return m_items; }
    QList<PageItem *> selection() const { return m_selection; }
    QRectF selectionRect() const; // page coordinates; invalid when empty

    // Selection changes are not undo steps; the content commands are.
    void setSelection(const QList<PageItem *> &items); // programmatic; the bench uses it
    void selectAll();
    void clearSelection();
    void deleteSelection();

    // The topmost item within a few screen pixels of the world point.
    PageItem *itemAt(const QPointF &worldPos) const;

    // Undo plumbing; the QUndoCommand classes in page.cpp call these.
    void addItem(PageItem *item);
    void removeItem(PageItem *item);
    // Erase with the quick fade. Idempotent: an erase drag already removed
    // its items live, so the command's redo must not re-fade them.
    void eraseItem(PageItem *item);
    void restoreItem(PageItem *item);

signals:
    void panningChanged();

protected:
    void hoverMoveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum class Drag { None, Marquee, Move, Resize, Erase };

    QPointF toWorld(const QPointF &pagePos) const;
    QRectF mapFromWorld(const QRectF &worldRect) const;
    QRectF worldSelectionBounds() const;
    void updateSelectionBox();
    int handleAt(const QPointF &pagePos) const;      // corner index or -1
    void updateHoverCursor(const QPointF &pagePos);

    void pressSelect(QMouseEvent *event);
    void startMove();
    void startResize(int corner);
    void resizeTo(const QPointF &worldPos, bool free);
    void updateMarquee(const QPointF &worldPos);
    void eraseAt(const QPointF &worldPos);
    void releaseMove();
    void releaseResize();
    void releaseErase();
    void cancelDrag();

    QPointF anchorPoint() const;
    QPointF posForZoom(qreal zoom, const QPointF &anchor) const;
    void setZoomAt(qreal zoom, const QPointF &anchor);
    void animateTo(qreal zoom, const QPointF &pos, int duration);
    void killAnim();

    QQuickItem *m_world;
    SelectionOverlay *m_overlay;
    QUndoStack *m_undo;
    QList<PageItem *> m_items;
    QList<PageItem *> m_selection;

    Stroke *m_stroke = nullptr; // stroke in progress
    Tools::Tool m_tool = Tools::Select;
    QColor m_ink = Qt::black;
    qreal m_strokeWidth = 2.75;

    // One pointer drag at a time; the payload members belong to it.
    Drag m_drag = Drag::None;
    QList<PageItem *> m_dragItems;    // selection snapshot while moving/resizing
    QList<QPointF> m_dragBasePos;     // their positions at drag start
    QPointF m_pressWorld;             // world point where the drag grabbed
    QPointF m_marqueeStart;           // world point where the marquee started
    QRectF m_marqueeRect;             // marquee in world coordinates
    QList<PageItem *> m_marqueeBase;  // selection to add to (Shift)
    QList<PageItem *> m_erased;       // items erased in the current drag
    int m_resizeCorner = 0;           // grabbed handle: 0=TL 1=TR 2=BR 3=BL
    QPointF m_resizeAnchor;           // the opposite corner (world)
    QPointF m_resizeDiagonal;         // grabbed corner minus anchor, at start
    QPointF m_resizeScale = QPointF(1, 1);

    qreal m_zoom = 1.0;
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPointF m_panGrab;  // page position where the pan drag started
    QPointF m_panStart; // world position at that moment
    QPointF m_mouse;    // last cursor position in page coordinates
    bool m_mouseSeen = false;
    QVariantAnimation *m_anim = nullptr;
};
