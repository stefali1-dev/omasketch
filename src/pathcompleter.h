#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

// Shell-like Tab completion for the path bar (decisions.md, "Path bar").
// tab() lists the folder in the typed path and returns the longest common
// prefix plus the rest of the first match as ghost text; another tab() on
// its own result cycles through the matches. The cycle state lives only
// here, keyed on the text tab() produced, so a Tab on a unique match
// re-lists and moves inside the folder, like a shell. Paths are shown with
// "~" for home; expand() turns them back into absolute ones.
class PathCompleter : public QObject
{
    Q_OBJECT

public:
    explicit PathCompleter(QObject *parent = nullptr);

    // One Tab press on text (step 1) or Shift+Tab (step -1); openMode
    // offers only folders and .png files. Returns {"text", "ghost", "count"}.
    Q_INVOKABLE QVariantMap tab(const QString &text, bool openMode, int step);
    // "~/x" -> "/home/user/x"; other paths pass through.
    Q_INVOKABLE QString expand(const QString &path) const;
    // "/home/user/x" -> "~/x" for display; other paths pass through.
    Q_INVOKABLE QString shorten(const QString &path) const;
    Q_INVOKABLE bool exists(const QString &path) const;
    Q_INVOKABLE bool isDir(const QString &path) const;
    // Deletes back from pos to the previous "/", space or start, taking a
    // trailing delimiter with it. Returns {"text", "cursor"}.
    Q_INVOKABLE QVariantMap killPrevWord(const QString &text, int pos) const;

private:
    QVariantMap complete(const QString &text, bool openMode);

    QString m_text;        // what the last tab() returned; a tab() on it cycles
    QString m_base;        // expanded folder the matches live in, with "/"
    QStringList m_matches; // file names, folders with a trailing "/"
    int m_index = -1;      // position while cycling; -1 until the first cycle
};
