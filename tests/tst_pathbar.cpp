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

#include "palette.h"
#include "page.h"
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
        {"page",   palette::page},
        {"ink",    palette::ink},
        {"red",    palette::red},
        {"blue",   palette::blue},
        {"ui",     palette::ui},
        {"accent", palette::accent},
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

// The real Toast.qml in a window, same context properties.
struct Toast
{
    QQmlEngine engine;
    QQuickWindow window;
    QQuickItem *item = nullptr;

    Toast()
    {
        engine.rootContext()->setContextProperty("Colors", paletteMap());
        QQmlComponent component(&engine,
                                QUrl::fromLocalFile(QStringLiteral(SRC_DIR) + "/Toast.qml"));
        item = qobject_cast<QQuickItem *>(component.create());
        if (!item)
            qFatal("Toast.qml failed to load: %s", qPrintable(component.errorString()));
        item->setParentItem(window.contentItem());
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
        const QVariantMap first = completer.tab(base + "de", false, 1);
        QCOMPARE(first["count"].toInt(), 2);
        QCOMPARE(first["text"].toString(), base + "demo");
        QCOMPARE(first["ghost"].toString(), "1.png");

        // A tab on the previous result cycles; Shift+Tab goes back.
        QVariantMap step = completer.tab(first["text"].toString(), false, 1);
        QCOMPARE(step["text"].toString(), base + "demo1.png");
        QCOMPARE(step["ghost"].toString(), QString());
        step = completer.tab(step["text"].toString(), false, 1);
        QCOMPARE(step["text"].toString(), base + "demo2.png");
        step = completer.tab(step["text"].toString(), false, -1);
        QCOMPARE(step["text"].toString(), base + "demo1.png");
        step = completer.tab(step["text"].toString(), false, -1);
        QCOMPARE(step["text"].toString(), base + "demo2.png");
    }

    void tabAfterUniqueCompletionListsInside()
    {
        // Regression (review): after a unique match the next tab() re-lists
        // and offers what is inside the folder, like a shell.
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/sub");
        touch(dir.path() + "/sub/inner1.png");
        touch(dir.path() + "/sub/inner2.png");

        PathCompleter completer;
        const QString base = dir.path() + '/';
        const QVariantMap first = completer.tab(base + "su", false, 1);
        QCOMPARE(first["count"].toInt(), 1);
        QCOMPARE(first["text"].toString(), base + "sub/");

        const QVariantMap second = completer.tab(first["text"].toString(), false, 1);
        QCOMPARE(second["count"].toInt(), 2);
        QCOMPARE(second["text"].toString(), base + "sub/inner");
        QCOMPARE(second["ghost"].toString(), "1.png");
        QCOMPARE(completer.tab(second["text"].toString(), false, 1)["text"].toString(),
                 base + "sub/inner1.png");
    }

    void foldersCompleteWithTrailingSlash()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/sub");

        PathCompleter completer;
        const QVariantMap result = completer.tab(dir.path() + "/su", false, 1);
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
        const QVariantMap open = completer.tab(dir.path() + '/', true, 1);
        QCOMPARE(open["count"].toInt(), 2);
        QCOMPARE(completer.tab(open["text"].toString(), true, 1)["text"].toString(),
                 dir.path() + "/folder/");
        QCOMPARE(completer.tab(dir.path() + "/folder/", true, 1)["text"].toString(),
                 dir.path() + "/picture.png");

        // In save mode everything is offered.
        QCOMPARE(completer.tab(dir.path() + '/', false, 1)["count"].toInt(), 3);
    }

    void tildeExpandsAndShortens()
    {
        PathCompleter completer;
        QCOMPARE(completer.expand("~"), QDir::homePath());
        QCOMPARE(completer.expand("~/x"), QDir::homePath() + "/x");
        QCOMPARE(completer.shorten(QDir::homePath() + "/x"), "~/x");
        QCOMPARE(completer.shorten(QDir::homePath()), "~");

        // "~" completes as the home folder, keeping the short display form.
        const QVariantMap result = completer.tab("~", false, 1);
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
        const QVariantMap result = completer.tab("~/Drawings/zz", false, 1);
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
        const QVariantMap result = completer.tab(dir.path() + "/f", false, 1);
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

    void tabAfterUniqueCompletionGoesThroughTheBar()
    {
        // Regression (review): after a unique match the next Tab must offer
        // what is inside the folder, like a shell, not go dead.
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/sub");
        touch(dir.path() + "/sub/inner1.png");
        touch(dir.path() + "/sub/inner2.png");

        Bar bar("save");
        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, dir.path() + "/su");
        QTest::keyClick(&bar.window, Qt::Key_Tab);
        QCOMPARE(bar.text(), dir.path() + "/sub/");
        QTest::keyClick(&bar.window, Qt::Key_Tab);
        QCOMPARE(bar.text(), dir.path() + "/sub/inner");
        QCOMPARE(bar.ghost(), "1.png");
        QTest::keyClick(&bar.window, Qt::Key_Tab);
        QCOMPARE(bar.text(), dir.path() + "/sub/inner1.png");
    }

    void ctrlWAndCtrlU()
    {
        Bar bar("save");
        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, "abc def/ghi");
        QCOMPARE(bar.text(), "abc def/ghi");

        QTest::keyClick(&bar.window, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(bar.text(), "abc def/");
        QTest::keyClick(&bar.window, Qt::Key_W, Qt::MetaModifier); // Super arrives as Meta too
        QCOMPARE(bar.text(), "abc ");
        QTest::keyClick(&bar.window, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(bar.text(), QString());

        type(&bar.window, "kept");
        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        QCOMPARE(bar.text(), QString());

        // Ctrl+U kills from the cursor to the start of the line, like readline.
        type(&bar.window, "keep tail");
        QTest::keyClick(&bar.window, Qt::Key_Left);
        QTest::keyClick(&bar.window, Qt::Key_Left);
        QTest::keyClick(&bar.window, Qt::Key_Left);
        QTest::keyClick(&bar.window, Qt::Key_Left);
        QTest::keyClick(&bar.window, Qt::Key_U, Qt::MetaModifier);
        QCOMPARE(bar.text(), "tail");
        QTest::keyClick(&bar.window, Qt::Key_X);
        QCOMPARE(bar.text(), "xtail"); // the cursor sits at the start
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

    void enterNeedsAFileName()
    {
        // Review: "" would confirm ".png" and a trailing "/" would confirm
        // "dir/.png" — Enter on either only notes, never confirms.
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/sub");
        Bar bar("save");
        QSignalSpy spy(bar.item, SIGNAL(confirmed(QString,QString)));

        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        QTest::keyClick(&bar.window, Qt::Key_Return); // empty line
        QVERIFY(bar.noteVisible());
        QCOMPARE(spy.count(), 0);

        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        type(&bar.window, dir.path() + "/sub/");      // ends in "/"
        QTest::keyClick(&bar.window, Qt::Key_Return);
        QVERIFY(bar.noteVisible());
        QCOMPARE(spy.count(), 0);
        QCOMPARE(bar.item->property("shown").toBool(), true);

        // Open mode notes on an empty line the same way.
        QMetaObject::invokeMethod(bar.item, "show", Q_ARG(QVariant, QStringLiteral("open")));
        QTest::keyClick(&bar.window, Qt::Key_U, Qt::ControlModifier);
        QTest::keyClick(&bar.window, Qt::Key_Return);
        QVERIFY(bar.noteVisible());
        QCOMPARE(spy.count(), 0);
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

        // Esc closes the bar without touching the tool. Checked from Draw
        // so the assertion can bite (review: from Select it was vacuous,
        // Select being the reset state).
        QTest::keyClick(&bar.window, Qt::Key_Escape);
        QTest::qWait(150);
        QCOMPARE(bar.opacity(), 0.0);
        QTest::keyClick(&bar.window, Qt::Key_D);
        QCOMPARE(bar.tools.tool(), Tools::Draw); // back on the canvas they fire again
        QMetaObject::invokeMethod(bar.item, "show", Q_ARG(QVariant, QStringLiteral("open")));
        QTest::keyClick(&bar.window, Qt::Key_Escape);
        QCOMPARE(bar.tools.tool(), Tools::Draw); // Esc in the bar left the tool alone
        QTest::keyClick(&bar.window, Qt::Key_Escape);
        QCOMPARE(bar.tools.tool(), Tools::Select); // on the canvas it resets
    }

    void shortcutKeysWhileTheBarIsFocused()
    {
        Bar bar("open");
        QSignalSpy spy(&bar.tools, &Tools::pathBarRequested);

        // While the bar has focus the shortcuts are ignored, so retyping
        // Ctrl+Shift+S does not wipe the path being typed (review).
        QTest::keyClick(&bar.window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyClick(&bar.window, Qt::Key_O, Qt::ControlModifier);
        QTest::keyClick(&bar.window, Qt::Key_S, Qt::MetaModifier | Qt::ShiftModifier);
        QCOMPARE(spy.count(), 0);

        // Back on the canvas they open the bar again, Ctrl and Super alike.
        QTest::keyClick(&bar.window, Qt::Key_Escape);
        QTest::keyClick(&bar.window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), "save");
        QTest::keyClick(&bar.window, Qt::Key_O, Qt::MetaModifier);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), "open");
    }

    void pageShortcutsAreGuardedToo()
    {
        Bar bar("open");

        // A page behind the bar, wired the way main.cpp wires it: the focus
        // guard must cover the draw shortcuts and Space-panning too, not
        // just the tool keys.
        bar.window.resize(800, 600);
        Page *page = new Page(bar.window.contentItem());
        page->setSize(QSizeF(800, 600));
        bar.tools.setPage(page);

        // On the canvas: Draw tool and a stroke as material for undo and
        // the fresh-page shortcut.
        QTest::keyClick(&bar.window, Qt::Key_Escape);
        QTest::qWait(150);
        QTest::keyClick(&bar.window, Qt::Key_D);
        QCOMPARE(bar.tools.tool(), Tools::Draw);
        QTest::mouseMove(&bar.window, {100, 300});
        QTest::mousePress(&bar.window, Qt::LeftButton, {}, {100, 300});
        QTest::mouseMove(&bar.window, {300, 300});
        QTest::mouseRelease(&bar.window, Qt::LeftButton, {}, {300, 300});
        QCOMPARE(page->strokes().size(), 1);

        // The bar is back with focus: none of it may reach the page.
        QMetaObject::invokeMethod(bar.item, "show", Q_ARG(QVariant, QStringLiteral("save")));
        QTest::keyPress(&bar.window, Qt::Key_Space);
        QVERIFY(bar.window.cursor().shape() != Qt::OpenHandCursor);
        QTest::keyRelease(&bar.window, Qt::Key_Space);
        const QPointF worldBefore = page->worldPos();
        QTest::keyClick(&bar.window, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(&bar.window, Qt::Key_N, Qt::ControlModifier);
        QTest::keyClick(&bar.window, Qt::Key_Plus, Qt::ControlModifier);
        QTest::keyClick(&bar.window, Qt::Key_0, Qt::ControlModifier);
        QTest::qWait(400); // an unguarded zoom step animates in 120 ms
        QCOMPARE(page->strokes().size(), 1);
        QCOMPARE(page->zoom(), 1.0);
        QCOMPARE(page->worldPos(), worldBefore);

        // Back on the canvas the same keys do reach the page.
        QTest::keyClick(&bar.window, Qt::Key_Escape);
        QTest::qWait(150);
        QTest::keyClick(&bar.window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(page->strokes().size(), 0);
    }

    void toastFadesInFastOutSlow()
    {
        // Review: the fade durations were swapped because a binding read
        // the animated opacity; they are now explicit, in 120 / out 150.
        Toast toast;
        QCOMPARE(toast.item->property("fadeInMs").toInt(), 120);
        QCOMPARE(toast.item->property("fadeOutMs").toInt(), 150);

        QMetaObject::invokeMethod(toast.item, "show",
                                  Q_ARG(QVariant, QStringLiteral("save → ~/x.png")));
        QCOMPARE(toast.item->property("text").toString(), "save → ~/x.png");
        QCOMPARE(toast.item->property("fadeMs").toInt(), 120); // the in-duration is chosen
        QTest::qWait(250);
        QCOMPARE(toast.item->property("opacity").toDouble(), 1.0);

        // Shorten the hold so the test sees the fade-out.
        auto *hold = toast.item->findChild<QObject *>("hold");
        QVERIFY(hold);
        hold->setProperty("interval", 30);
        QMetaObject::invokeMethod(hold, "restart");
        QTest::qWait(100);
        QCOMPARE(toast.item->property("fadeMs").toInt(), 150); // the out-duration is chosen
        QTest::qWait(300);
        QCOMPARE(toast.item->property("opacity").toDouble(), 0.0);
    }
};

QTEST_MAIN(PathBarTest)
#include "tst_pathbar.moc"
