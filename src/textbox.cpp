#include "textbox.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtQuick/qsgtextnode.h>
#include <QVariant>
#include <cmath>

#include "palette.h"

namespace {

// The QML engine and context of the page this box lives on: the box is
// created from C++ with only a visual parent, so both must be found up that
// chain. The editor needs the context to be created, and the caret delegate
// to be instantiated inside it.
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

// The editor: Qt's own TextEdit (caret, selection, input methods) with the
// knobs that never change. The C++ TextEdit class is private, so the editor
// is created through the public QML API and text, colour and font go over
// properties; the tests pin all three down.
QByteArray editorData()
{
    return QStringLiteral(
                "import QtQuick\n"
                "TextEdit {\n"
                "    readOnly: false\n"
                "    selectByMouse: true\n"
                "    wrapMode: TextEdit.NoWrap\n"
                "    renderType: TextEdit.QtRendering\n"
                "}\n")
        .toUtf8();
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
    // The paint node adds the document during the scene graph sync; the
    // layout must exist by then, or it is created lazily inside the sync
    // and the first layout pass crashes.
    m_document->documentLayout();
}

void TextBox::applyFont()
{
    m_font = QFont(QStringLiteral("JetBrainsMono Nerd Font"));
    // Below a pixel the text renders at 1 px, but m_fontSize itself stays
    // true, so resizing back out — or undo — restores the real size.
    m_font.setPixelSize(qMax(1, qRound(m_fontSize)));
    m_document->setDefaultFont(m_font);
    if (m_editor)
        m_editor->setProperty("font", m_font);
}

QFont TextBox::font() const
{
    if (m_editing && m_editor)
        return m_editor->property("font").value<QFont>();
    return m_font;
}

void TextBox::setText(const QString &text)
{
    m_document->setPlainText(text);
    update();
}

int TextBox::cursorPosition() const
{
    return m_editing && m_editor ? m_editor->property("cursorPosition").toInt() : -1;
}

void TextBox::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    if (m_editor)
        m_editor->setProperty("color", color);
    update();
}

QQuickItem *TextBox::ensureEditor()
{
    if (m_editor) {
        m_editor->setPosition(QPointF(0, 0));
        return m_editor;
    }
    QQmlEngine *engine = engineFor(this);
    if (!engine) {
        qWarning("omasketch: text box without a QML engine cannot edit");
        return nullptr;
    }
    QQmlComponent component(engine);
    component.setData(editorData(), QUrl("textbox-editor"));
    QQmlContext *context = contextFor(this);
    m_editor = qobject_cast<QQuickItem *>(component.create(context));
    if (!m_editor) {
        qWarning("omasketch: text editor failed to create: %s",
                 qPrintable(component.errorString()));
        return nullptr;
    }
    m_editor->setParent(this);     // ownership
    m_editor->setParentItem(this); // into the scene; focus needs a window
    m_editor->setPosition(QPointF(0, 0));
    // Bounds follow the live text while editing.
    connect(m_editor, SIGNAL(textChanged()), this, SIGNAL(boundsChanged()));
    // The accent caret, monkeytype-like; the component must outlive the
    // editor because the delegate item is created when the caret first shows.
    m_cursorDelegate = new QQmlComponent(engine, this);
    m_cursorDelegate->setData(cursorDelegateData(), QUrl("textbox-caret"));
    m_editor->setProperty("cursorDelegate",
                          QVariant::fromValue(m_cursorDelegate));
    return m_editor;
}

void TextBox::startEdit(const QPointF &localPress)
{
    if (m_editing)
        return;
    QQuickItem *editor = ensureEditor();
    if (!editor)
        return;
    editor->setProperty("text", text());
    editor->setProperty("color", m_color);
    editor->setProperty("font", m_font);
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
    setText(m_editor->property("text").toString());
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
    // Multiplicative and unclamped, so undo restores exactly; the 1 px
    // floor lives in applyFont's rendering, not here.
    m_fontSize *= std::sqrt(sx * sy);
    applyFont();
    update();
}

QRectF TextBox::localBounds() const
{
    // While editing the text lives in the editor; its implicit size is the
    // laid-out content.
    const QSizeF size = m_editing && m_editor
        ? QSizeF(m_editor->implicitWidth(), m_editor->implicitHeight())
        : m_document->size();
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
