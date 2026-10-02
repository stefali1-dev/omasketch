#pragma once

#include <QObject>
#include <QRectF>
#include <QString>
#include <QUrl>

class Page;
class QQuickItem;

// Margin around the drawing's bounds in the exported PNG, in world units.
constexpr qreal kExportMargin = 24;

// Renders the real items inside worldRect (world coordinates) into a white
// QImage at `scale` device pixels per world unit, by moving the world into a
// private offscreen render window for the duration of the call. The visible
// page never renders inside this stack, so what's on screen never changes,
// and the result is independent of the view's pan and zoom.
QImage renderWorldToImage(QQuickItem *world, const QRectF &worldRect, qreal scale);

// Saving and opening PNGs (decisions.md, "Files"). Ctrl+S keeps one target:
// the first save goes to ~/Pictures/Drawings/<timestamp>.png, later saves
// overwrite it; a save-as from the path bar moves the target. Opens, pastes
// and drops place the PNG as an image item. Closing with unsaved changes
// quietly auto-saves. The page changes only through Page's undoable API, so
// an unclean undo stack is exactly "unsaved changes".
class Files : public QObject
{
    Q_OBJECT

public:
    explicit Files(QObject *parent = nullptr);

    void setPage(Page *page);

    // Ctrl+S.
    Q_INVOKABLE void save();
    // The path bar's confirmed(path, mode); mode is "save" or "open".
    Q_INVOKABLE void confirm(const QString &path, const QString &mode);
    // Ctrl+C: the selection as a PNG, or the whole drawing.
    Q_INVOKABLE void copySelection();
    // Ctrl+V: the clipboard image at the mouse position.
    Q_INVOKABLE void paste();
    // A dropped file from a file manager.
    Q_INVOKABLE void openDrop(const QUrl &url);

    // `omasketch file.png`; main.cpp calls it after the first frame.
    void openFile(const QString &path);
    // The window is closing: quietly auto-save unsaved changes.
    void appClosing();

    QString saveTarget() const { return m_target; } // tests

signals:
    // Feedback lines for the toast ("saved → ~/…", "nothing to save").
    void toastRequested(const QString &message);

private:
    void saveTo(const QString &path, bool toast);
    void openImage(const QImage &image);
    bool writePng(const QRectF &worldRect, const QString &path);
    QRectF exportRect() const;     // drawing bounds plus margin
    QString defaultPath() const;   // ~/Pictures/Drawings/<timestamp>.png

    Page *m_page = nullptr;
    QString m_target;              // the file later Ctrl+S updates
};
