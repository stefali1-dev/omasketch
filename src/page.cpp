#include "page.h"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPropertyAnimation>
#include <QUndoCommand>
#include <QUndoStack>
#include <QQuickWindow>
#include <QSet>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QtMath>

#include "arrow.h"
#include "imageitem.h"
#include "pageitem.h"
#include "palette.h"
#include "selection.h"
#include "stroke.h"
#include "textbox.h"

namespace {

constexpr qreal kZoomMin = 0.1;
constexpr qreal kZoomMax = 8.0;
constexpr qreal kZoomStep = 1.25; // one Ctrl+= press, one Super+scroll notch
constexpr int kStepEase = 120;    // ms per zoom step
constexpr int kHomeEase = 150;    // ms back to the drawing
constexpr int kFade = 80;         // ms for the selection box and erase fades

constexpr qreal kHitTolerance = 6; // screen pixels a click may miss a line by
constexpr qreal kMinDrag = 4;      // screen pixels a drag must cover to draw

qreal clampScale(qreal s)
{
    // Resizing never mirrors: a handle dragged past the anchor stops at a
    // small positive scale, the kinds that cannot flip included. Away from
    // zero, so a full collapse could still be undone by rescaling.
    return s > 0.01 ? s : 0.01;
}

} // namespace

// Owns its item for as long as it sits on the stack; the page adds it to and
// removes it from the world as the stack moves through time. Every command
// below only references items; their AddItem keeps them alive.
class AddItem : public QUndoCommand
{
public:
    AddItem(Page *page, PageItem *item)
        : m_page(page), m_item(item)
    {
        setText("draw");
    }

    ~AddItem() override { delete m_item; }

    void undo() override { m_page->removeItem(m_item); }
    void redo() override { m_page->addItem(m_item); } // redo re-adds: a no-op

private:
    Page *m_page;
    PageItem *m_item;
};

class MoveItems : public QUndoCommand
{
public:
    MoveItems(const QList<PageItem *> &items, const QList<QPointF> &from,
              const QList<QPointF> &to)
        : m_items(items), m_from(from), m_to(to)
    {
        setText("move");
    }

    void undo() override
    {
        for (int i = 0; i < m_items.size(); ++i)
            m_items[i]->setPosition(m_from[i]);
    }

    void redo() override
    {
        for (int i = 0; i < m_items.size(); ++i)
            m_items[i]->setPosition(m_to[i]);
    }

private:
    QList<PageItem *> m_items;
    QList<QPointF> m_from;
    QList<QPointF> m_to;
};

// The drag left the items under a live scale transform; this command replaces
// it with the real thing: the geometry is rescaled once and the transform is
// gone. Positions carry the anchor, so scaleGeometry needs no anchor.
class ResizeItems : public QUndoCommand
{
public:
    ResizeItems(const QList<PageItem *> &items, const QList<QPointF> &from,
                const QList<QPointF> &to, QPointF scale)
        : m_items(items), m_from(from), m_to(to), m_scale(scale)
    {
        setText("resize");
    }

    void undo() override { apply(1 / m_scale.x(), 1 / m_scale.y(), m_from); }
    void redo() override { apply(m_scale.x(), m_scale.y(), m_to); }

private:
    void apply(qreal sx, qreal sy, const QList<QPointF> &pos)
    {
        for (int i = 0; i < m_items.size(); ++i) {
            m_items[i]->scaleGeometry(sx, sy);
            m_items[i]->setPosition(pos[i]);
        }
    }

    QList<PageItem *> m_items;
    QList<QPointF> m_from;
    QList<QPointF> m_to;
    QPointF m_scale;
};

class DeleteItems : public QUndoCommand
{
public:
    DeleteItems(Page *page, const QList<PageItem *> &items)
        : m_page(page)
    {
        // Topmost first, each with its stacking position: redo then removes
        // without shifting the positions still to remove, and undo can slot
        // every item back exactly where it was.
        const QList<PageItem *> stack = page->items();
        for (int i = stack.size() - 1; i >= 0; --i) {
            if (items.contains(stack[i])) {
                m_items.append(stack[i]);
                m_indices.append(i);
            }
        }
        setText("delete");
    }

    void undo() override
    {
        for (int i = m_items.size() - 1; i >= 0; --i)
            m_page->restoreItem(m_items[i], m_indices[i]);
    }

    void redo() override
    {
        for (PageItem *item : m_items)
            m_page->removeItem(item);
    }

private:
    Page *m_page;
    QList<PageItem *> m_items; // topmost first
    QList<int> m_indices;      // stacking position each item was at
};

class RecolorItems : public QUndoCommand
{
public:
    RecolorItems(const QList<PageItem *> &items, const QList<QColor> &from,
                 const QColor &to)
        : m_items(items), m_from(from), m_to(to)
    {
        setText("recolor");
    }

    void undo() override
    {
        for (int i = 0; i < m_items.size(); ++i)
            m_items[i]->setColor(m_from[i]);
    }

    void redo() override
    {
        for (PageItem *item : m_items)
            item->setColor(m_to);
    }

private:
    QList<PageItem *> m_items;
    QList<QColor> m_from;
    QColor m_to;
};

class EraseItems : public QUndoCommand
{
public:
    // The items in the order the drag erased them (topmost down), with the
    // stacking position each was at: undo re-adds them back to front, so
    // erased strokes come back under the ones that stayed.
    EraseItems(Page *page, const QList<PageItem *> &items, const QList<int> &indices)
        : m_page(page), m_items(items), m_indices(indices)
    {
        setText("erase");
    }

    void undo() override
    {
        for (int i = m_items.size() - 1; i >= 0; --i)
            m_page->restoreItem(m_items[i], m_indices[i]);
    }

    void redo() override
    {
        for (PageItem *item : m_items)
            m_page->eraseItem(item); // idempotent: the drag already removed these
    }

private:
    Page *m_page;
    QList<PageItem *> m_items;
    QList<int> m_indices;
};

// One edit of an existing box. Editing the text down to empty removes the
// box in the same step, so undo puts it back with its text; the box itself
// stays owned by its AddItem, this command only moves it in and out of the
// page.
class SetText : public QUndoCommand
{
public:
    SetText(Page *page, TextBox *box, const QString &from, const QString &to)
        : m_page(page), m_box(box), m_from(from), m_to(to)
    {
        m_index = page->items().indexOf(box);
        setText("edit text");
    }

    void undo() override
    {
        m_box->setText(m_from);
        if (m_to.isEmpty())
            m_page->restoreItem(m_box, m_index);
    }

    void redo() override
    {
        if (m_to.isEmpty())
            m_page->removeItem(m_box);
        else
            m_box->setText(m_to);
    }

private:
    Page *m_page;
    TextBox *m_box;
    QString m_from;
    QString m_to;
    int m_index = 0; // stacking position for the undo of a removal
};

Page::Page(QQuickItem *parent)
    : QQuickItem(parent)
    , m_world(new QQuickItem(this))
    , m_overlay(new SelectionOverlay(this))
    , m_undo(new QUndoStack(this))
{
    // Scale and position are applied around the world's top-left corner so the
    // anchor math below stays in one place.
    m_world->setTransformOrigin(QQuickItem::TopLeft);
    // The overlay must never take the mouse; the page handles every click.
    m_overlay->setAcceptedMouseButtons(Qt::NoButton);
    m_overlay->setVisible(false);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

Page::~Page()
{
    // Commands own their strokes; drop the stack before the item tree goes.
    delete m_undo;
    delete m_drawing; // an item in progress never reached the stack and a
                      // visual parent does not own it
    // Nor does a box being edited — unless it was committed earlier: then
    // its AddItem on the stack (destroyed above) already freed it.
    if (m_editing && !m_items.contains(m_editing))
        delete m_editing;
}

void Page::setTool(Tools::Tool tool)
{
    if (m_editing)
        commitEditing(); // keys went to the box; this path cannot run mid-edit
    cancelDrag();
    if (tool != Tools::Select)
        clearSelection();
    m_tool = tool;
    updateSelectionBox();
    updateHoverCursor(m_mouse);
}

void Page::setInk(Tools::Ink ink)
{
    QColor color;
    switch (ink) {
    case Tools::Black: color = palette::ink;  break;
    case Tools::Red:   color = palette::red;  break;
    case Tools::Blue:  color = palette::blue; break;
    }
    // 1/2/3 recolour the selection as well as setting the ink; one undo step.
    // Runs even when the ink already matches — the selection may differ —
    // and only mint a command when some selected item actually changes.
    QList<PageItem *> changed;
    QList<QColor> from;
    for (PageItem *item : m_selection) {
        if (item->recolourable() && item->color() != color) {
            changed.append(item);
            from.append(item->color());
        }
    }
    if (!changed.isEmpty())
        m_undo->push(new RecolorItems(changed, from, color));
    if (m_ink == color)
        return;
    m_ink = color;
}

void Page::undo()
{
    if (!m_drawing && m_drag == Drag::None && !m_editing)
        m_undo->undo();
    updateSelectionBox(); // a move or resize under the box may have been undone
}

void Page::redo()
{
    if (!m_drawing && m_drag == Drag::None && !m_editing)
        m_undo->redo();
    updateSelectionBox();
}

void Page::newPage()
{
    if (m_drawing || m_items.isEmpty() || m_drag != Drag::None || m_editing)
        return;
    clearSelection();
    m_undo->push(new DeleteItems(this, m_items));
}

bool Page::escape()
{
    if (m_editing) {
        commitEditing(); // the box stays, the tool stays
        return true;
    }
    if (m_drag == Drag::None && m_selection.isEmpty())
        return false;
    cancelDrag(); // a held drag must not commit after the Esc
    clearSelection();
    return true;
}

void Page::zoomStep(int direction)
{
    if (m_drawing || m_panning)
        return;
    const qreal target = m_zoom * qPow(kZoomStep, direction);
    animateTo(target, posForZoom(target, anchorPoint()), kStepEase);
}

void Page::zoomHome()
{
    if (m_drawing || m_panning)
        return;
    const QRectF bounds = drawingBounds();
    const QPointF target = bounds.isValid()
        ? QPointF(width() / 2, height() / 2) - bounds.center() // drawing centred
        : QPointF(0, 0);                                       // the start position
    animateTo(1.0, target, kHomeEase);
}

void Page::setSpaceHeld(bool held)
{
    // A press mid-stroke still lands here; the mouse handlers decide that no
    // pan can start until the stroke is done.
    m_spaceHeld = held;
}

void Page::pinch(QNativeGestureEvent *event)
{
    if (event->gestureType() != Qt::ZoomNativeGesture || m_drawing || m_panning
        || m_drag != Drag::None)
        return;
    setZoomAt(m_zoom * (1 + event->value()), mapFromScene(event->scenePosition()));
}

void Page::addItem(PageItem *item)
{
    if (!m_items.contains(item))
        m_items.append(item);
    item->setParentItem(m_world);
}

void Page::removeItem(PageItem *item)
{
    m_items.removeOne(item);
    m_selection.removeOne(item);
    item->setParentItem(nullptr);
    updateSelectionBox();
}

void Page::eraseItem(PageItem *item)
{
    if (!m_items.contains(item))
        return; // already erased; the command's redo must not re-fade it
    // Gone from the page immediately, but it keeps rendering until the fade
    // is done, so the detach is deferred to the animation's end.
    m_items.removeOne(item);
    m_selection.removeOne(item);
    updateSelectionBox();
    auto *fade = new QPropertyAnimation(item, "opacity", item);
    fade->setDuration(kFade);
    fade->setStartValue(item->opacity());
    fade->setEndValue(0.0);
    connect(fade, &QPropertyAnimation::finished, item, [this, item] {
        item->setParentItem(nullptr);
        item->setOpacity(1.0);
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void Page::restoreItem(PageItem *item, int index)
{
    // Stop a fade that is still running (an undo right after a redo).
    const QList<QPropertyAnimation *> fades = item->findChildren<QPropertyAnimation *>();
    for (QPropertyAnimation *fade : fades)
        fade->stop(); // DeleteWhenStopped
    item->setOpacity(1.0);
    // Back at the old stacking position: right below its successor in the
    // list, which is the z-order the world's children follow.
    if (index < 0 || index >= m_items.size())
        m_items.append(item);
    else
        m_items.insert(index, item);
    item->setParentItem(m_world);
    const int at = m_items.indexOf(item);
    if (at + 1 < m_items.size())
        item->stackBefore(m_items[at + 1]);
}

void Page::addImage(const QImage &image, const QPointF &worldPos, QSizeF size)
{
    if (image.isNull() || m_drawing || m_drag != Drag::None)
        return;
    auto *item = new ImageItem;
    item->setImage(image, size.isValid() ? size : QSizeF(image.size()));
    item->setPosition(worldPos);
    m_undo->push(new AddItem(this, item)); // redo re-adds it: a no-op
}

QRectF Page::drawingBounds() const
{
    QRectF bounds;
    for (const PageItem *item : m_items) {
        if (bounds.isValid())
            bounds = bounds.united(item->bounds());
        else
            bounds = item->bounds();
    }
    return bounds;
}

QRectF Page::selectionRect() const
{
    const QRectF world = worldSelectionBounds();
    return world.isValid() ? mapFromWorld(world) : QRectF();
}

void Page::setSelection(const QList<PageItem *> &items)
{
    m_selection = items;
    updateSelectionBox();
}

void Page::selectAll()
{
    setSelection(m_items);
}

void Page::clearSelection()
{
    m_selection.clear();
    updateSelectionBox();
}

void Page::deleteSelection()
{
    if (m_selection.isEmpty() || m_drawing || m_drag != Drag::None || m_editing)
        return;
    const QList<PageItem *> items = m_selection;
    clearSelection(); // not an undo step
    m_undo->push(new DeleteItems(this, items));
}

void Page::hoverMoveEvent(QHoverEvent *event)
{
    m_mouse = event->position();
    m_mouseSeen = true;
    updateHoverCursor(event->position());
}

void Page::mousePressEvent(QMouseEvent *event)
{
    m_mouse = event->position();
    m_mouseSeen = true;
    if (event->button() != Qt::LeftButton || m_drawing)
        return;
    if (m_spaceHeld) {
        m_panning = true;
        killAnim(); // the drag owns the world position now, as with the wheel
        m_panGrab = event->position();
        m_panStart = m_world->position();
        emit panningChanged();
        event->accept();
        return;
    }
    // Any editing press lands outside the box (inside, the editor child
    // takes it): it commits first, whichever tool acts on it.
    if (m_editing)
        commitEditing();
    switch (m_tool) {
    case Tools::Draw: {
        auto *stroke = new Stroke;
        stroke->setColor(m_ink);
        stroke->setStrokeWidth(m_strokeWidth);
        // While drawing, the stroke lives on the page under a mirror of the
        // world's transform: its geometry is rebuilt on every pointer move,
        // and as a world child each rebuild would re-upload the world's big
        // merged batches (a hitch every few frames on full pages). The world
        // cannot move during a stroke - pan and zoom are blocked - so the
        // mirror holds; finishDrawing returns the finished stroke to the
        // world.
        stroke->setParentItem(this);
        stroke->setPosition(m_world->position());
        stroke->setScale(m_zoom);
        stroke->begin(toWorld(event->position()));
        m_drawing = stroke;
        event->accept();
        break;
    }
    case Tools::Arrow: {
        auto *arrow = new Arrow;
        arrow->setColor(m_ink);
        arrow->setStrokeWidth(m_strokeWidth);
        arrow->setParentItem(this); // the stroke's mirror, same reason
        arrow->setPosition(m_world->position());
        arrow->setScale(m_zoom);
        m_pressWorld = toWorld(event->position());
        arrow->begin(m_pressWorld);
        m_drawing = arrow;
        event->accept();
        break;
    }
    case Tools::Eraser:
        m_drag = Drag::Erase;
        m_erased.clear();
        eraseAt(toWorld(event->position()));
        event->accept();
        break;
    case Tools::Select:
        pressSelect(event);
        break;
    case Tools::Text:
        pressText(event);
        break;
    }
}

void Page::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Same handling: pressSelect tells a double-click from a single one by
    // the event type.
    mousePressEvent(event);
}

void Page::mouseMoveEvent(QMouseEvent *event)
{
    m_mouse = event->position();
    m_mouseSeen = true;
    if (m_panning) {
        m_world->setPosition(m_panStart + event->position() - m_panGrab);
        event->accept();
        return;
    }
    if (m_drawing) {
        const QPointF world = toWorld(event->position());
        if (auto *arrow = qobject_cast<Arrow *>(m_drawing))
            arrow->setEnd(event->modifiers() & Qt::ShiftModifier
                              ? arrow->snapped(world) : world);
        else
            static_cast<Stroke *>(m_drawing)->addPoint(world);
        event->accept();
        return;
    }
    switch (m_drag) {
    case Drag::Marquee:
        updateMarquee(toWorld(event->position()));
        event->accept();
        return;
    case Drag::Move: {
        // Follows the pointer exactly; positions change, geometry does not.
        const QPointF delta = toWorld(event->position()) - m_pressWorld;
        for (int i = 0; i < m_dragItems.size(); ++i)
            m_dragItems[i]->setPosition(m_dragBasePos[i] + delta);
        updateSelectionBox();
        event->accept();
        return;
    }
    case Drag::Resize:
        resizeTo(toWorld(event->position()),
                 event->modifiers() & Qt::ShiftModifier);
        event->accept();
        return;
    case Drag::Erase:
        eraseAt(toWorld(event->position()));
        event->accept();
        return;
    default:
        break;
    }
}

void Page::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_panning) {
        m_panning = false;
        emit panningChanged();
        event->accept();
        return;
    }
    if (m_drawing) {
        finishDrawing(toWorld(event->position()),
                      event->modifiers() & Qt::ShiftModifier);
        event->accept();
        return;
    }
    switch (m_drag) {
    case Drag::Move:    releaseMove();   break;
    case Drag::Resize:  releaseResize(); break;
    case Drag::Erase:   releaseErase();  break;
    case Drag::Marquee: break; // the live selection stands
    case Drag::None:    return;
    }
    m_drag = Drag::None;
    m_dragItems.clear();
    m_dragBasePos.clear();
    m_erased.clear();
    m_erasedIndices.clear();
    updateSelectionBox(); // marquee box becomes the selection box
    updateHoverCursor(m_mouse);
    event->accept();
}

void Page::wheelEvent(QWheelEvent *event)
{
    if (m_drawing || m_panning)
        return;
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & (Qt::ControlModifier | Qt::MetaModifier)) {
        if (m_drag != Drag::None)
            return; // zooming mid-drag shifts the world under the drag
        // Super+scroll arrives as Ctrl+wheel, one 120-notch per step.
        setZoomAt(m_zoom * qPow(kZoomStep, event->angleDelta().y() / 120.0),
                  event->position());
        event->accept();
        return;
    }
    QPointF delta = event->pixelDelta();
    if (delta.isNull())
        delta = QPointF(event->angleDelta()); // one wheel notch (120) pans 120 px
    // Adding the delta pans with the scroll. The horizontal sign looks wrong
    // against Qt's docs (positive x = rotated right), but QtWayland delivers
    // the Wayland "+x is right" value negated (KDE bug 417604): scroll right
    // arrives negative, and adding it moves the view right, like every other
    // app on this stack (verified on a real window).
    const QPointF pan = mods & Qt::ShiftModifier
        ? QPointF(delta.x() != 0 ? delta.x() : delta.y(), 0)
        : delta;
    if (!pan.isNull()) {
        killAnim();
        m_world->setPosition(m_world->position() + pan);
    }
    event->accept();
}

QPointF Page::toWorld(const QPointF &pagePos) const
{
    return (pagePos - m_world->position()) / m_zoom;
}

QRectF Page::mapFromWorld(const QRectF &worldRect) const
{
    return QRectF(m_world->position() + worldRect.topLeft() * m_zoom,
                  worldRect.size() * m_zoom);
}

QRectF Page::worldSelectionBounds() const
{
    QRectF bounds;
    for (const PageItem *item : m_selection) {
        if (bounds.isValid())
            bounds = bounds.united(item->bounds());
        else
            bounds = item->bounds();
    }
    // A few screen pixels of air so the outline never sits on the ink; the
    // handles sit on these padded corners.
    if (bounds.isValid())
        bounds.adjust(-kBoxPad / m_zoom, -kBoxPad / m_zoom,
                      kBoxPad / m_zoom, kBoxPad / m_zoom);
    return bounds;
}

void Page::updateSelectionBox()
{
    const bool marquee = m_drag == Drag::Marquee && m_marqueeRect.isValid();
    if (!marquee && m_selection.isEmpty()) {
        m_overlay->setVisible(false);
        return;
    }
    QRectF world;
    if (marquee)
        world = m_marqueeRect;
    else
        world = worldSelectionBounds(); // bounds() carries the resize scale
    const bool wasHidden = !m_overlay->isVisible();
    m_overlay->setHandles(!marquee);
    m_overlay->setVisible(true);
    if (marquee) {
        m_overlay->setOpacity(1.0);
    } else if (wasHidden) {
        m_overlay->setOpacity(0.0);
        m_overlay->appear(); // quick fade when the selection box appears
    }
    m_overlay->setRect(mapFromWorld(world));
}

PageItem *Page::itemAt(const QPointF &worldPos) const
{
    const qreal tolerance = kHitTolerance / m_zoom; // a few screen pixels
    for (int i = m_items.size() - 1; i >= 0; --i) { // topmost first
        if (m_items[i]->hitTest(worldPos, tolerance))
            return m_items[i];
    }
    return nullptr;
}

int Page::handleAt(const QPointF &pagePos) const
{
    const QRectF rect = selectionRect();
    if (!rect.isValid())
        return -1;
    // The nearest corner within grab range, not the first one: on a flat
    // selection two handles can both be in range a few pixels apart.
    const QPointF corners[4] = {rect.topLeft(), rect.topRight(),
                                rect.bottomRight(), rect.bottomLeft()};
    int nearest = -1;
    qreal best = kHandleGrab;
    for (int i = 0; i < 4; ++i) {
        const qreal d = QLineF(pagePos, corners[i]).length();
        if (d <= best) {
            best = d;
            nearest = i;
        }
    }
    return nearest;
}

void Page::updateHoverCursor(const QPointF &pagePos)
{
    // Dragging tools own their cursors (the resize drag keeps the diagonal).
    if (m_tool != Tools::Select || m_drag != Drag::None || m_drawing || m_panning
        || m_spaceHeld || !window()) {
        return;
    }
    const int corner = handleAt(pagePos);
    window()->setCursor(corner < 0 ? Qt::ArrowCursor
                        : corner == 0 || corner == 2 ? Qt::SizeFDiagCursor
                                                     : Qt::SizeBDiagCursor);
}

void Page::pressSelect(QMouseEvent *event)
{
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    m_pressWorld = toWorld(event->position());
    // A double-click opens a text box for editing, caret at the click.
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (TextBox *box = qobject_cast<TextBox *>(itemAt(m_pressWorld))) {
            cancelDrag(); // the first press of the click started a move
            startEditing(box, false, m_pressWorld - box->position());
            event->accept();
            return;
        }
    }
    const int corner = handleAt(event->position());
    if (corner >= 0) {
        startResize(corner);
        event->accept();
        return;
    }
    PageItem *item = itemAt(m_pressWorld);
    if (item && shift) {
        // Shift+click adds or removes; no drag starts.
        if (m_selection.contains(item))
            m_selection.removeOne(item);
        else
            m_selection.append(item);
        updateSelectionBox();
        event->accept();
        return;
    }
    // Inside the selection box the whole selection moves, ink or not (the
    // Figma and Excalidraw convention); a click without a drag keeps it.
    if (!shift && !m_selection.isEmpty()
        && selectionRect().contains(event->position())) {
        startMove();
        event->accept();
        return;
    }
    if (item) {
        if (!m_selection.contains(item))
            setSelection({item});
        startMove();
        event->accept();
        return;
    }
    // Empty page: a marquee. A click without a drag clears the selection.
    m_drag = Drag::Marquee;
    m_marqueeStart = m_pressWorld;
    m_marqueeRect = QRectF();
    m_marqueeBase = shift ? m_selection : QList<PageItem *>();
    if (!shift)
        m_selection.clear();
    updateSelectionBox(); // an old box must not linger until the first move
    if (window())
        window()->setCursor(Qt::CrossCursor);
    event->accept();
}

// The text tool on the page: a click inside the box being edited never
// lands here (the editor child takes it and moves the caret natively); any
// other click commits the edit first, then edits the box clicked on or
// places a new one.
void Page::pressText(QMouseEvent *event)
{
    const QPointF world = toWorld(event->position());
    TextBox *box = qobject_cast<TextBox *>(itemAt(world));
    const bool isNew = !box;
    if (!box) {
        box = new TextBox;
        box->setColor(m_ink);
        box->setPosition(world);
        box->setParentItem(m_world);
    }
    startEditing(box, isNew, world - box->position());
    event->accept();
}

void Page::startEditing(TextBox *box, bool isNew, const QPointF &localPress)
{
    cancelDrag();
    m_editing = box;
    m_editingNew = isNew;
    m_editingBefore = box->text();
    connect(box, &TextBox::editingChanged, this, [this, box] {
        if (m_editing == box && !box->isEditing())
            commitEditing(); // the box stopped editing itself (Esc)
    });
    // The live text sits in the editor; the page redraws the box's overlay
    // as its bounds follow the typing.
    connect(box, &TextBox::boundsChanged, this, [this] { updateSelectionBox(); });
    box->startEdit(localPress);
}

void Page::commitEditing()
{
    if (!m_editing)
        return;
    TextBox *box = m_editing;
    m_editing = nullptr; // first: the stopEdit signal must not re-enter
    disconnect(box, nullptr, this, nullptr); // the edit's two connections
    box->stopEdit();
    if (m_editingNew) {
        if (box->text().isEmpty()) {
            box->setParentItem(nullptr);
            box->deleteLater(); // never entered the page: no undo step
        } else {
            addItem(box);
            m_undo->push(new AddItem(this, box)); // one step with its text
        }
    } else if (box->text() != m_editingBefore) {
        m_undo->push(new SetText(this, box, m_editingBefore, box->text()));
    }
    m_editingNew = false;
}

void Page::warmTextEditor()
{
    // The editor's QML is parsed and instantiated on first creation, a
    // ~60 ms hit that would otherwise land in the first keystroke frame
    // of a real edit. A throwaway box takes the hit invisibly: created
    // and freed between two frames, so nothing renders, nothing gets
    // focus for long, and nothing reaches the undo stack.
    TextBox *box = new TextBox(this);
    box->startEdit(QPointF(0, 0));
    box->stopEdit();
    delete box;
}

void Page::finishDrawing(const QPointF &world, bool snap)
{
    if (auto *arrow = qobject_cast<Arrow *>(m_drawing)) {
        arrow->setEnd(snap ? arrow->snapped(world) : world);
        // kMinDrag counts screen pixels: the drag as the screen sees it.
        if (QLineF(m_pressWorld, world).length() * m_zoom < kMinDrag) {
            delete m_drawing; // a click is nothing, like an empty text box
            m_drawing = nullptr;
            return;
        }
    } else {
        static_cast<Stroke *>(m_drawing)->addPoint(world); // the final build
    }
    // Back into the world at identity: the per-frame uploads are done and
    // the batch can merge the finished item with the rest of the page.
    m_drawing->setPosition(QPointF(0, 0));
    m_drawing->setScale(1);
    m_drawing->setParentItem(m_world);
    m_undo->push(new AddItem(this, m_drawing)); // redo re-adds it: a no-op
    m_drawing = nullptr;
}

void Page::startMove()
{
    m_drag = Drag::Move;
    m_dragItems = m_selection;
    m_dragBasePos.clear();
    m_dragBasePos.reserve(m_dragItems.size());
    for (PageItem *item : m_dragItems)
        m_dragBasePos.append(item->position());
}

void Page::startResize(int corner)
{
    m_drag = Drag::Resize;
    m_resizeCorner = corner;
    const QRectF world = worldSelectionBounds();
    const QPointF corners[4] = {world.topLeft(), world.topRight(),
                                world.bottomRight(), world.bottomLeft()};
    m_resizeAnchor = corners[(corner + 2) % 4]; // resize around the opposite corner
    m_resizeDiagonal = corners[corner] - m_resizeAnchor;
    m_resizeScale = QPointF(1, 1);
    m_dragItems = m_selection;
    m_dragBasePos.clear();
    for (PageItem *item : m_dragItems)
        m_dragBasePos.append(item->position());
    if (window()) {
        window()->setCursor(corner == 0 || corner == 2 ? Qt::SizeFDiagCursor
                                                       : Qt::SizeBDiagCursor);
    }
}

void Page::resizeTo(const QPointF &worldPos, bool free)
{
    qreal sx;
    qreal sy;
    if (free) {
        sx = clampScale((worldPos.x() - m_resizeAnchor.x()) / m_resizeDiagonal.x());
        sy = clampScale((worldPos.y() - m_resizeAnchor.y()) / m_resizeDiagonal.y());
    } else {
        // Proportions kept: one scale from the diagonal distance, so the
        // handle follows the pointer however it moves.
        const qreal s = qMax(0.01, QLineF(worldPos, m_resizeAnchor).length()
                                       / QLineF(m_resizeDiagonal, QPointF(0, 0)).length());
        sx = sy = s;
    }
    m_resizeScale = QPointF(sx, sy);
    for (int i = 0; i < m_dragItems.size(); ++i) {
        m_dragItems[i]->setVisualScale(QPointF(sx, sy));
        const QPointF &base = m_dragBasePos[i];
        m_dragItems[i]->setPosition(QPointF(m_resizeAnchor.x() + sx * (base.x() - m_resizeAnchor.x()),
                                            m_resizeAnchor.y() + sy * (base.y() - m_resizeAnchor.y())));
    }
    updateSelectionBox();
}

void Page::updateMarquee(const QPointF &worldPos)
{
    m_marqueeRect = QRectF(m_marqueeStart, worldPos).normalized();
    QSet<PageItem *> selected(m_marqueeBase.cbegin(), m_marqueeBase.cend());
    for (PageItem *item : m_items) {
        if (!selected.contains(item) && item->touchesRect(m_marqueeRect))
            selected.insert(item);
    }
    m_selection = QList<PageItem *>(selected.cbegin(), selected.cend());
    updateSelectionBox();
}

void Page::eraseAt(const QPointF &worldPos)
{
    const qreal radius = kEraserDiameter / (2 * m_zoom);
    for (int i = m_items.size() - 1; i >= 0; --i) { // backwards: eraseItem removes
        PageItem *item = m_items[i];
        if (item->hitTest(worldPos, radius)) {
            m_erased.append(item);
            m_erasedIndices.append(i);
            eraseItem(item);
        }
    }
}

void Page::releaseMove()
{
    QList<QPointF> end;
    end.reserve(m_dragItems.size());
    bool moved = false;
    for (int i = 0; i < m_dragItems.size(); ++i) {
        end.append(m_dragItems[i]->position());
        moved = moved || end.constLast() != m_dragBasePos[i];
    }
    if (moved)
        m_undo->push(new MoveItems(m_dragItems, m_dragBasePos, end));
}

void Page::releaseResize()
{
    // Drop the live scale; the pushed command bakes it into the geometry
    // once (its redo runs here, with the geometry still unscaled).
    for (PageItem *item : m_dragItems)
        item->setVisualScale(QPointF(1, 1));
    if (m_resizeScale != QPointF(1, 1)) {
        QList<QPointF> end;
        end.reserve(m_dragItems.size());
        for (PageItem *item : m_dragItems)
            end.append(item->position());
        m_undo->push(new ResizeItems(m_dragItems, m_dragBasePos, end, m_resizeScale));
    }
}

void Page::releaseErase()
{
    if (!m_erased.isEmpty())
        m_undo->push(new EraseItems(this, m_erased, m_erasedIndices));
}

void Page::cancelDrag()
{
    if (m_drag == Drag::Erase && !m_erased.isEmpty()) {
        // Keep what the drag already erased undoable.
        m_undo->push(new EraseItems(this, m_erased, m_erasedIndices));
    }
    if (m_drag == Drag::Move || m_drag == Drag::Resize) {
        for (int i = 0; i < m_dragItems.size(); ++i) {
            m_dragItems[i]->setPosition(m_dragBasePos[i]);
            m_dragItems[i]->setVisualScale(QPointF(1, 1));
        }
    }
    m_drag = Drag::None;
    m_dragItems.clear();
    m_dragBasePos.clear();
    m_erased.clear();
    m_erasedIndices.clear();
}

QPointF Page::anchorPoint() const
{
    return m_mouseSeen ? m_mouse : QPointF(width() / 2, height() / 2);
}

QPointF Page::posForZoom(qreal zoom, const QPointF &anchor) const
{
    // Keep the world point under the anchor on screen: anchor = pos + world*zoom.
    const QPointF world = (anchor - m_world->position()) / m_zoom;
    return anchor - world * zoom;
}

void Page::setZoomAt(qreal zoom, const QPointF &anchor)
{
    killAnim();
    zoom = qBound(kZoomMin, zoom, kZoomMax);
    const QPointF pos = posForZoom(zoom, anchor); // reads the current zoom
    m_zoom = zoom;
    m_world->setScale(zoom);
    m_world->setPosition(pos);
}

void Page::animateTo(qreal zoom, const QPointF &pos, int duration)
{
    killAnim();
    zoom = qBound(kZoomMin, zoom, kZoomMax);
    const qreal z0 = m_zoom;
    const QPointF p0 = m_world->position();
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(duration);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this,
            [this, z0, p0, zoom, pos](const QVariant &t) {
                const qreal f = t.toReal();
                m_zoom = z0 * qPow(zoom / z0, f); // interpolate zoom geometrically
                m_world->setScale(m_zoom);
                m_world->setPosition(p0 + (pos - p0) * f);
            });
    m_anim->start();
}

void Page::killAnim()
{
    delete m_anim;
    m_anim = nullptr;
}
