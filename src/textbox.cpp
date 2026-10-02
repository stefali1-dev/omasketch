#include "textbox.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtQuick/private/qquicktextedit_p.h>
#include <QtQuick/qsgtextnode.h>
#include <cmath>

#include "palette.h"

namespace {

// The QML engine and context of the page this box lives on: the box is
// created from C++ with only a visual parent, so both must be found up that
// chain. The editor needs the context to instantiate the caret delegate.
QQmlEngine *engineFor(const QQuickItem *item)
{
    for (const QQuickItem *o = item; o; o = o->parentItem()) {
        if (QQmlEngine *engine = qmlEngine(o))
            return engine;
    }
    return nullptr;
}

QQmlContext *contextFor(const QQuickItem *item)
{
    for (const QQuickItem *o = item; o; o = o->parentItem()) {
        if (QQmlContext *context = qmlContext(o))
            return context;
    }
    return nullptr;
}

// The caret is a thin accent bar, monkeytype-like. It needs the QML engine
// (the delegate is a QQmlComponent), so engine-less tests keep Qt's default.
QByteArray cursorDelegateData()
{
    return QStringLiteral(
                "import QtQuick\n"
                "Rectangle { width: 2; radius: 1; color: \"%1\" }")
        .arg(palette::accent.name())
        .toUtf8();
}

} // namespace

TextBox::TextBox(QQuickItem *parent)
    : PageItem(parent)
    , m_document(std::make_unique<QTextDocument>())
{
    setFlag(ItemHasContents);
    m_document->setDocumentMargin(0);
    applyFont();
    // The paint node adds the document from the render thread; the layout
    // must exist by then, or it is created lazily on the wrong thread and
    // the first layout pass crashes.
    m_document->documentLayout();
}

void TextBox::applyFont()
{
    m_font = QFont(QStringLiteral("JetBrainsMono Nerd Font"));
    m_font.setPixelSize(qRound(m_fontSize));
    m_document->setDefaultFont(m_font);
    if (m_editor)
        m_editor->setFont(m_font);
}

void TextBox::setText(const QString &text)
{
    m_document->setPlainText(text);
    update();
}

int TextBox::cursorPosition() const
{
    return m_editing && m_editor ? m_editor->cursorPosition() : -1;
}

void TextBox::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    if (m_editor)
        m_editor->setColor(color);
    update();
}

QQuickTextEdit *TextBox::ensureEditor()
{
    if (m_editor) {
        m_editor->setPosition(QPointF(0, 0));
        return m_editor;
    }
    m_editor = new QQuickTextEdit(this);
    m_editor->setPosition(QPointF(0, 0));
    m_editor->setReadOnly(false);
    m_editor->setSelectByMouse(true);
    m_editor->setWrapMode(QQuickTextEdit::NoWrap);
    // Same distance-field pipeline as the idle text node, so entering and
    // leaving an edit does not change how the glyphs look.
    m_editor->setRenderType(QQuickTextEdit::QtRendering);
    if (QQmlEngine *engine = engineFor(this)) {
        // Without a context the editor cannot instantiate the delegate
        // component: it falls back to its own, unset, context.
        if (QQmlContext *context = contextFor(this))
            QQmlEngine::setContextForObject(m_editor, context);
        m_cursorDelegate = new QQmlComponent(engine, this);
        m_cursorDelegate->setData(cursorDelegateData(), QUrl("textbox-caret"));
        m_editor->setCursorDelegate(m_cursorDelegate);
    }
    return m_editor;
}

void TextBox::startEdit(const QPointF &localPress)
{
    if (m_editing)
        return;
    QQuickTextEdit *editor = ensureEditor();
    editor->setText(text());
    editor->setColor(m_color);
    editor->setVisible(true);
    editor->forceActiveFocus();

    // The page owns every real click; a synthesized one lets the editor
    // place the caret natively where this edit was started.
    if (window()) {
        const QPointF global = window()->mapToGlobal(mapToScene(localPress));
        QMouseEvent press(QEvent::MouseButtonPress, localPress, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QGuiApplication::sendEvent(editor, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, localPress, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QGuiApplication::sendEvent(editor, &release);
    }

    m_editing = true;
    update(); // the editor draws the text now; the idle node steps aside
    emit editingChanged();
}

void TextBox::stopEdit()
{
    if (!m_editing)
        return;
    setText(m_editor->text());
    m_editor->setFocus(false);
    m_editor->setVisible(false);
    m_editing = false;
    update();
    emit editingChanged();
}

void TextBox::keyPressEvent(QKeyEvent *event)
{
    // Esc reaches this box only while it is editing: the editor child has
    // focus and ignores the key, and it propagates up the parent chain. The
    // page hears about the end through editingChanged and commits.
    if (event->key() == Qt::Key_Escape) {
        stopEdit();
        event->accept();
        return;
    }
    QQuickItem::keyPressEvent(event);
}

bool TextBox::hitTest(const QPointF &worldPos, qreal tolerance) const
{
    // A box is solid: anywhere on it, plus a little air, is a hit.
    return bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance)
        .contains(worldPos);
}

void TextBox::scaleGeometry(qreal sx, qreal sy)
{
    // Text scales as a unit: the font grows, the box never stretches. A
    // free (Shift) resize lands on the geometric mean of the two axes.
    m_fontSize = qMax<qreal>(1.0, m_fontSize * std::sqrt(sx * sy));
    applyFont();
    update();
}

QRectF TextBox::localBounds() const
{
    const QSizeF size = m_document->size();
    const QFontMetricsF metrics(m_font);
    // An empty box still has a caret-sized area to click on.
    return QRectF(0, 0,
                  qMax<qreal>(size.width(), metrics.horizontalAdvance(u'0')),
                  qMax<qreal>(size.height(), metrics.height()));
}

QRectF TextBox::boundingRect() const
{
    // Like a stroke: covers the live drag scale, so the scene graph culls
    // whole boxes during a resize.
    return scaledBounds();
}

QSGNode *TextBox::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // While the editor child draws the text this item draws nothing; the
    // node comes back when the edit ends.
    if (m_editing || m_document->isEmpty()) {
        delete oldNode;
        return nullptr;
    }
    auto *node = static_cast<QSGTextNode *>(oldNode);
    if (!node) {
        node = window()->createTextNode();
        node->setRenderType(QSGTextNode::QtRendering);
    }
    node->clear();
    node->setColor(m_color);
    node->addTextDocument(QPointF(0, 0), m_document.get());
    return node;
}
