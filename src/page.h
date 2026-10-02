#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QQuickItem>
#include <QUndoStack>
#include <QtQml/qqml.h>

#include "tools.h"

class QNativeGestureEvent;
class QUndoStack;
class QVariantAnimation;
class Arrow;
class PageItem;
class SelectionOverlay;
class Stroke;
class TextBox;

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

    // Esc, first job: cancel an in-flight drag, then clear the selection.
    // False when there was neither, so the caller can fall back to the
    // tool switch (the second Esc job).
    bool escape();

    bool isPanning() const { return m_panning; }
    bool isDrawing() const { return m_drawing != nullptr; }
    // The item being drawn, while it is (lives on the page, not the world:
    // see the Draw case in mousePressEvent). Introspection for the tests.
    PageItem *drawing() const { return m_drawing; }
    // The box being edited, or null. Not committed, so not in items() yet.
    TextBox *editing() const { return m_editing; }

    // Throws away one text editor right after startup, so its QML is
    // parsed outside any real edit (see main.cpp for when).
    void warmTextEditor();

    // Introspection for the tests and the frame-time bench.
    qreal zoom() const { return m_zoom; }
    QPointF worldPos() const { return m_world->position(); }
    QPointF mouse() const { return m_mouse; } // last cursor position, page coords
    QQuickItem *worldItem() const { return m_world; }
    QRectF drawingBounds() const;
    QList<PageItem *> items() const { return m_items; }
    QList<PageItem *> selection() const { return m_selection; }
    QRectF selectionRect() const; // page coordinates; invalid when empty

    // Undo-bookkeeping for the save flow: "unsaved" is an unclean stack,
    // and saving marks it clean.
    bool isClean() const { return m_undo->isClean(); }
    void markClean() { m_undo->setClean(); }

    // Places a decoded image (open, paste, drop) as an undoable image item
    // with its top-left at worldPos; an invalid size keeps the 1:1 pixel size.
    void addImage(const QImage &image, const QPointF &worldPos,
                  QSizeF size = QSizeF());

    // Selection changes are not undo steps; the content commands are.
    void setSelection(const QList<PageItem *> &items); // programmatic; the bench uses it
    void selectAll();
    void clearSelection();
    void deleteSelection();
    // Stops the box being edited and makes the edit an undo step; nothing
    // happens when no box is editing. The shortcuts that still work while
    // typing (save/open path bar, fresh page) and the close-time autosave
    // call this first.
    void commitEditing();

    // The topmost item within a few screen pixels of the world point.
    PageItem *itemAt(const QPointF &worldPos) const;

    // Undo plumbing; the QUndoCommand classes in page.cpp call these.
    void addItem(PageItem *item);
    void removeItem(PageItem *item);
    // Erase with the quick fade. Idempotent: an erase drag already removed
    // its items live, so the command's redo must not re-fade them.
    void eraseItem(PageItem *item);
    // Undo of a delete or erase: stop a fade still running, then put the
    // item back at its old stacking position (-1 appends).
    void restoreItem(PageItem *item, int index);

signals:
    void panningChanged();

protected:
    void hoverMoveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    // Qt Quick delivers the second click of a double-click here, not to
    // mousePressEvent; the select tool opens text boxes with it.
    void mouseDoubleClickEvent(QMouseEvent *event) override;
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
    void pressText(QMouseEvent *event);
    // Opens the box for editing; localPress places the caret at the click.
    void startEditing(TextBox *box, bool isNew, const QPointF &localPress);
    // Commits the item being drawn (stroke or arrow) as an undo step; a
    // click without a real drag makes nothing. snap bends an arrow to 45°.
    void finishDrawing(const QPointF &world, bool snap);
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

    PageItem *m_drawing = nullptr; // item being drawn: a Stroke or an Arrow
    Tools::Tool m_tool = Tools::Select;
    QColor m_ink = Qt::black;
    qreal m_strokeWidth = 2.75;
    TextBox *m_editing = nullptr; // box being edited; not committed yet
    bool m_editingNew = false;    // it would be created by this edit
    QString m_editingBefore;      // its text before the edit, for the undo

    // One pointer drag at a time; the payload members belong to it.
    Drag m_drag = Drag::None;
    QList<PageItem *> m_dragItems;    // selection snapshot while moving/resizing
    QList<QPointF> m_dragBasePos;     // their positions at drag start
    QPointF m_pressWorld;             // world point where the drag grabbed
    QPointF m_marqueeStart;           // world point where the marquee started
    QRectF m_marqueeRect;             // marquee in world coordinates
    QList<PageItem *> m_marqueeBase;  // selection to add to (Shift)
    QList<PageItem *> m_erased;       // items erased in the current drag
    QList<int> m_erasedIndices;       // their stacking positions, for the undo
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
