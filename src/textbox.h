#pragma once

#include <QColor>
#include <QFont>
#include <QTextDocument>
#include <memory>

#include "pageitem.h"

class QQmlComponent;

// One text box: plain multiline JetBrains Mono text in the current ink.
// Idle, the text is drawn by a scene graph text node built from the box's
// own QTextDocument; editing happens in a Qt TextEdit child (created through
// the public QML API — the C++ TextEdit class is private), which exists from
// the first edit on and is hidden between edits. The page starts and commits
// edits — a committed box carries its final text, so creating one is a plain
// AddItem — and the box only reports that an edit started or ended.
class TextBox : public PageItem
{
    Q_OBJECT

public:
    explicit TextBox(QQuickItem *parent = nullptr);

    bool isEditing() const { return m_editing; }
    QString text() const { return m_document->toPlainText(); }
    void setText(const QString &text);
    // The font the text is laid out with, idle or while editing; the tests
    // pin down that both are the monospace the design calls for.
    QFont font() const;
    qreal fontSize() const { return m_fontSize; }
    // Current caret position in the box being edited; -1 when idle. The
    // tests use it to pin down where a click started the caret.
    int cursorPosition() const;

    // The page calls these on a press; localPress is the click in item
    // coordinates, so the caret starts where the box was clicked.
    void startEdit(const QPointF &localPress);
    void stopEdit();

    bool hitTest(const QPointF &worldPos, qreal tolerance) const override;
    void scaleGeometry(qreal sx, qreal sy) override;

    QColor color() const override { return m_color; }
    void setColor(const QColor &color) override;

signals:
    void editingChanged();
    // The live text changed the box's size while editing; the page keeps
    // the selection box and hit tests on it.
    void boundsChanged();

protected:
    QRectF localBounds() const override;
    QRectF boundingRect() const override;
    void keyPressEvent(QKeyEvent *event) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    QQuickItem *ensureEditor();
    void applyFont();

    QColor m_color = Qt::black;
    qreal m_fontSize = 19; // reads well next to 2.75 px strokes at 100% zoom
    QFont m_font;
    std::unique_ptr<QTextDocument> m_document;
    QQuickItem *m_editor = nullptr;       // created with the first edit
    QQmlComponent *m_cursorDelegate = nullptr;
    bool m_editing = false;
};
