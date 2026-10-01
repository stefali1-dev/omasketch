#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "pathcompleter.h"
#include "tools.h"

namespace {

void touch(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        qFatal("could not create %s", qPrintable(path));
    file.write("x");
}

void type(QQuickWindow *window, const QString &text)
{
    for (const QChar c : text)
        QTest::keyClick(window, c.toLatin1());
}

QVariantMap paletteMap()
{
    return {
        {"page",   QColor(0xff, 0xff, 0xff)},
        {"ink",    QColor(0x00, 0x00, 0x00)},
        {"red",    QColor(0xe0, 0x31, 0x31)},
        {"blue",   QColor(0x19, 0x71, 0xc2)},
        {"ui",     QColor(0x64, 0x66, 0x69)},
        {"accent", QColor(0xb3, 0x91, 0x10)},
    };
}

// The real PathBar.qml in a window, with the context properties main.cpp
// sets. Declare the engine first so the item (owned by the window) dies
// before the engine does.
struct Bar
{
    QQmlEngine engine;
    PathCompleter completer;
    Tools tools;
    QQuickWindow window;
    QQuickItem *item = nullptr;

    explicit Bar(const QString &mode)
    {
        engine.rootContext()->setContextProperty("Colors", paletteMap());
        engine.rootContext()->setContextProperty("completer", &completer);
        engine.rootContext()->setContextProperty("tools", &tools);
        tools.attach(&window);

        QQmlComponent component(&engine,
                                QUrl::fromLocalFile(QStringLiteral(SRC_DIR) + "/PathBar.qml"));
        item = qobject_cast<QQuickItem *>(component.create());
        if (!item)
            qFatal("PathBar.qml failed to load: %s", qPrintable(component.errorString()));
        item->setParentItem(window.contentItem());
        QMetaObject::invokeMethod(item, "show", Q_ARG(QVariant, mode));
    }

    QString text() const
    {
        return item->findChild<QQuickItem *>("input")->property("text").toString();
    }

    QString ghost() const
    {
        return item->findChild<QQuickItem *>("ghost")->property("text").toString();
    }

    // Reads the bar's state properties, not Item::visible: inside a
    // never-shown test window that one reports effective visibility.
    bool noteVisible() const
    {
        return item->property("noteShown").toBool();
    }

    double opacity() const
    {
        return item->property("opacity").toDouble();
    }
};

} // namespace

class PathBarTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- PathCompleter ----

    void completesCommonPrefixAndCycles()
    {
        QTemporaryDir dir;
        touch(dir.path() + "/demo1.png");
        touch(dir.path() + "/demo2.png");
        touch(dir.path() + "/other.png");

        PathCompleter completer;
        const QString base = dir.path() + '/';
        const QVariantMap first = completer.complete(base + "de", false);
        QCOMPARE(first["count"].toInt(), 2);
        QCOMPARE(first["text"].toString(), base + "demo");
        QCOMPARE(first["ghost"].toString(), "1.png");

        QVariantMap step = completer.cycle(1);
        QCOMPARE(step["text"].toString(), base + "demo1.png");
        QCOMPARE(step["ghost"].toString(), QString());
        step = completer.cycle(1);
        QCOMPARE(step["text"].toString(), base + "demo2.png");
        step = completer.cycle(-1);
        QCOMPARE(step["text"].toString(), base + "demo1.png");
        step = completer.cycle(-1);
        QCOMPARE(step["text"].toString(), base + "demo2.png");
    }

    void foldersCompleteWithTrailingSlash()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/sub");

        PathCompleter completer;
        const QVariantMap result = completer.complete(dir.path() + "/su", false);
        QCOMPARE(result["count"].toInt(), 1);
        QCOMPARE(result["text"].toString(), dir.path() + "/sub/");
    }

    void openModeOffersFoldersAndPngOnly()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/folder");
        touch(dir.path() + "/picture.png");
        touch(dir.path() + "/notes.txt");

        PathCompleter completer;
        const QVariantMap open = completer.complete(dir.path() + '/', true);
        QCOMPARE(open["count"].toInt(), 2);
        QCOMPARE(completer.cycle(1)["text"].toString(), dir.path() + "/folder/");
        QCOMPARE(completer.cycle(1)["text"].toString(), dir.path() + "/picture.png");

        // In save mode everything is offered.
        QCOMPARE(completer.complete(dir.path() + '/', false)["count"].toInt(), 3);
    }

    void tildeExpandsAndShortens()
    {
        PathCompleter completer;
        QCOMPARE(completer.expand("~"), QDir::homePath());
        QCOMPARE(completer.expand("~/x"), QDir::homePath() + "/x");
        QCOMPARE(completer.shorten(QDir::homePath() + "/x"), "~/x");
        QCOMPARE(completer.shorten(QDir::homePath()), "~");

        // "~" completes as the home folder, keeping the short display form.
        const QVariantMap result = completer.complete("~", false);
        QVERIFY(result["count"].toInt() >= 1);
        QVERIFY(result["text"].toString().startsWith("~/"));
    }

    void tildePathWithSubfolderCompletes()
    {
        // Regression (found in the real-window check): a "~/"-path with
        // several segments must split at the last slash, not after the tilde.
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/Drawings");
        touch(dir.path() + "/Drawings/zzdemo1.png");
        touch(dir.path() + "/Drawings/zzdemo2.png");

        const QString realHome = QDir::homePath();
        qputenv("HOME", dir.path().toUtf8());
        PathCompleter completer;
        const QVariantMap result = completer.complete("~/Drawings/zz", false);
        qputenv("HOME", realHome.toUtf8());

        QCOMPARE(result["count"].toInt(), 2);
        QCOMPARE(result["text"].toString(), "~/Drawings/zzdemo");
        QCOMPARE(result["ghost"].toString(), "1.png");
    }

    void listingStaysWithinAFrame()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 3000; ++i)
            touch(dir.path() + QString("/f%1.png").arg(i, 4, 10, QChar('0')));

        PathCompleter completer;
        QElapsedTimer timer;
        timer.start();
        const QVariantMap result = completer.complete(dir.path() + "/f", false);
        QVERIFY(timer.nsecsElapsed() < 8'000'000); // one frame at 120 Hz
        QCOMPARE(result["count"].toInt(), 3000);
    }

    void killPrevWordDeletesSegmentOrWord()
    {
        PathCompleter completer;
        QVariantMap result = completer.killPrevWord("abc def/ghi", 11);
        QCOMPARE(result["text"].toString(), "abc def/");
        QCOMPARE(result["cursor"].toInt(), 8);
        result = completer.killPrevWord("abc def/", 8);
        QCOMPARE(result["text"].toString(), "abc ");
        result = completer.killPrevWord("abc ", 4);
        QCOMPARE(result["text"].toString(), QString());
    }

    // ---- The real PathBar.qml ----

    void tabCompletesThroughTheBar()
    {
        QTemporaryDir dir;
        touch(dir.path() + "/demo1.png");
        touch(dir.path() + "/demo2.png");

        Bar bar("save");
        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, dir.path() + "/de");
        QTest::keyClick(&bar.window, Qt::Key_Tab);
        QCOMPARE(bar.text(), dir.path() + "/demo");
        QCOMPARE(bar.ghost(), "1.png");

        // Tab accepts the first match, Shift+Tab goes back one.
        QTest::keyClick(&bar.window, Qt::Key_Tab);
        QCOMPARE(bar.text(), dir.path() + "/demo1.png");
        QCOMPARE(bar.ghost(), QString());
        QTest::keyClick(&bar.window, Qt::Key_Backtab);
        QCOMPARE(bar.text(), dir.path() + "/demo2.png");
    }

    void ctrlWAndCtrlU()
    {
        Bar bar("save");
        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, "abc def/ghi");
        QCOMPARE(bar.text(), "abc def/ghi");

        QTest::keyClick(&bar.window, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(bar.text(), "abc def/");
        QTest::keyClick(&bar.window, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(bar.text(), "abc ");
        QTest::keyClick(&bar.window, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(bar.text(), QString());

        type(&bar.window, "kept");
        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        QCOMPARE(bar.text(), QString());
    }

    void overwriteNeedsSecondEnter()
    {
        QTemporaryDir dir;
        touch(dir.path() + "/me.png");
        Bar bar("save");
        QSignalSpy spy(bar.item, SIGNAL(confirmed(QString,QString)));

        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, dir.path() + "/me");
        QTest::keyClick(&bar.window, Qt::Key_Return);
        QVERIFY(bar.noteVisible());
        QCOMPARE(spy.count(), 0);

        // Editing disarms the overwrite.
        type(&bar.window, "x");
        QTest::keyClick(&bar.window, Qt::Key_Backspace);
        QVERIFY(!bar.noteVisible());
        QTest::keyClick(&bar.window, Qt::Key_Return);
        QVERIFY(bar.noteVisible());
        QCOMPARE(spy.count(), 0);

        QTest::keyClick(&bar.window, Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), dir.path() + "/me.png");
        QCOMPARE(spy.first().at(1).toString(), "save");
    }

    void openModeNotesInsteadOfConfirming()
    {
        QTemporaryDir dir;
        touch(dir.path() + "/real.png");
        Bar bar("open");
        QSignalSpy spy(bar.item, SIGNAL(confirmed(QString,QString)));

        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, dir.path() + "/ghost.png");
        QTest::keyClick(&bar.window, Qt::Key_Return);
        QVERIFY(bar.noteVisible());
        QCOMPARE(spy.count(), 0);

        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, dir.path());
        QTest::keyClick(&bar.window, Qt::Key_Return);
        QVERIFY(bar.noteVisible());
        QCOMPARE(spy.count(), 0);

        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, dir.path() + "/real.png");
        QTest::keyClick(&bar.window, Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), dir.path() + "/real.png");
        QCOMPARE(spy.first().at(1).toString(), "open");

        QTest::qWait(150);
        QCOMPARE(bar.opacity(), 0.0);
    }

    void confirmExpandsTildeAndAddsPng()
    {
        Bar bar("save");
        QSignalSpy spy(bar.item, SIGNAL(confirmed(QString,QString)));

        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, "~/omasketch-test/x");
        QTest::keyClick(&bar.window, Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QDir::homePath() + "/omasketch-test/x.png");
        QCOMPARE(spy.first().at(1).toString(), "save");
    }

    void toolKeysGoIntoTheBar()
    {
        Bar bar("open");

        QTest::keyClick(&bar.window, Qt::Key_D);
        QCOMPARE(bar.tools.tool(), Tools::Select);
        QVERIFY(bar.text().endsWith("d"));

        QTest::keyClick(&bar.window, Qt::Key_Escape);
        QTest::qWait(150);
        QCOMPARE(bar.opacity(), 0.0);
        QCOMPARE(bar.tools.tool(), Tools::Select); // Esc cancelled the bar, not the tool

        QTest::keyClick(&bar.window, Qt::Key_D);
        QCOMPARE(bar.tools.tool(), Tools::Draw); // back on the canvas they fire again
        QTest::keyClick(&bar.window, Qt::Key_Escape);
        QCOMPARE(bar.tools.tool(), Tools::Select);
    }

    void shortcutKeysReachTools()
    {
        Bar bar("open");
        QSignalSpy spy(&bar.tools, &Tools::pathBarRequested);

        QTest::keyClick(&bar.window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), "save");

        QTest::keyClick(&bar.window, Qt::Key_O, Qt::ControlModifier);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), "open");

        // Super arrives as Meta instead of Ctrl just the same.
        QTest::keyClick(&bar.window, Qt::Key_S, Qt::MetaModifier | Qt::ShiftModifier);
        QCOMPARE(spy.count(), 3);
    }
};

QTEST_MAIN(PathBarTest)
#include "tst_pathbar.moc"
