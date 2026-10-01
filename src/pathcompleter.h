#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

// Shell-like Tab completion for the path bar (decisions.md, "Path bar").
// complete() lists the folder in the typed path and returns the longest
// common prefix plus the rest of the first match as ghost text; cycle()
// steps through the matches. Paths are shown with "~" for home; expand()
// turns them back into absolute ones. All methods are QML-facing.
class PathCompleter : public QObject
{
    Q_OBJECT

public:
    explicit PathCompleter(QObject *parent = nullptr);

    // Completes the last segment of text; openMode offers only folders and
    // .png files. Returns {"text", "ghost", "count"}.
    Q_INVOKABLE QVariantMap complete(const QString &text, bool openMode);
    // Another Tab (step 1) or Shift+Tab (step -1) over the last complete().
    Q_INVOKABLE QVariantMap cycle(int step);
    // Drops the stale matches after the text changed.
    Q_INVOKABLE void reset();
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
    QString m_base;        // expanded folder the matches live in, with "/"
    QStringList m_matches; // file names, folders with a trailing "/"
    int m_index = -1;      // position while cycling; -1 until the first cycle
};
