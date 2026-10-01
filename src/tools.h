#pragma once

#include <QObject>

class QEvent;
class QQuickWindow;

// The current tool and ink colour, moved by the plain keys from decisions.md.
// Attached to the window it also keeps the cursor matching the tool, and it is
// where the Ctrl/Super shortcuts will land later.
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

    Tool tool() const { return m_tool; }
    Ink ink() const { return m_ink; }
    QString toolName() const;

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void toolChanged();
    void inkChanged();

private:
    void setTool(Tool tool);
    void setInk(Ink ink);
    void applyCursor();

    QQuickWindow *m_window = nullptr;
    Tool m_tool = Select;
    Ink m_ink = Black;
};
