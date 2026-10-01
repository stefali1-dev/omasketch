#pragma once

#include <QObject>

class QEvent;
class Page;
class QQuickWindow;

// Diameter of the eraser cursor in screen pixels; page.cpp erases everything
// within half of it.
constexpr qreal kEraserDiameter = 20.0;

// The current tool and ink colour, moved by the plain keys from decisions.md.
// Attached to the window it also keeps the cursor matching the tool, runs the
// Ctrl/Super shortcuts (undo, zoom, fresh page), tracks the Space key for
// panning, and forwards window-level touchpad gestures to the page.
class Tools : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Tool tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(Ink ink READ ink NOTIFY inkChanged)
    Q_PROPERTY(QString toolName READ toolName NOTIFY toolChanged)

public:
    enum Tool { Select, Draw, Text, Eraser, Arrow };
    Q_ENUM(Tool)
    enum Ink { Black, Red, Blue };
    Q_ENUM(Ink)

    explicit Tools(QObject *parent = nullptr);

    void attach(QQuickWindow *window);
    void setPage(Page *page);

    Tool tool() const { return m_tool; }
    Ink ink() const { return m_ink; }
    QString toolName() const;

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void toolChanged();
    void inkChanged();
    // Ctrl+Shift+S / Ctrl+O (Super arrives as either Ctrl or Meta).
    void pathBarRequested(const QString &mode);

private:
    void setTool(Tool tool);
    void setInk(Ink ink);
    void applyCursor();
    void setSpaceHeld(bool held);

    QQuickWindow *m_window = nullptr;
    Page *m_page = nullptr;
    Tool m_tool = Select;
    Ink m_ink = Black;
    bool m_spaceHeld = false;
};

// The Page QML declared inside Main.qml. Items in a Window are
// QObject-parented to the window itself; only their visual parent is the
// content item, so the search must start at the window.
Page *pageIn(QQuickWindow *window);
