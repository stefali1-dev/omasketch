#include <QtQuick/QQuickWindow>
#include <QtTest>

#include "tools.h"

class SkeletonTest : public QObject
{
    Q_OBJECT

private slots:
    void keyPressChangesTool();
    void fileShortcutsEmitSignals();
};

void SkeletonTest::fileShortcutsEmitSignals()
{
    QQuickWindow window;
    Tools tools;
    tools.attach(&window);

    QSignalSpy save(&tools, &Tools::saveRequested);
    QSignalSpy copy(&tools, &Tools::copyRequested);
    QSignalSpy paste(&tools, &Tools::pasteRequested);
    QSignalSpy pathBar(&tools, &Tools::pathBarRequested);

    QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(save.size(), 1);
    QCOMPARE(copy.size(), 1);
    QCOMPARE(paste.size(), 1);
    QCOMPARE(pathBar.size(), 0);

    QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(&window, Qt::Key_O, Qt::ControlModifier);
    QCOMPARE(pathBar.size(), 2);
    QCOMPARE(pathBar.constFirst().constFirst().toString(), "save");
    QCOMPARE(pathBar.constLast().constFirst().toString(), "open");

    // Super counts as Ctrl (decisions.md).
    QTest::keyClick(&window, Qt::Key_S, Qt::MetaModifier);
    QCOMPARE(save.size(), 2);

    // The plain keys stay tool keys.
    QTest::keyClick(&window, Qt::Key_V);
    QCOMPARE(tools.tool(), Tools::Select);
    QCOMPARE(paste.size(), 1);
}

void SkeletonTest::keyPressChangesTool()
{
    QQuickWindow window;
    Tools tools;
    tools.attach(&window);

    QCOMPARE(tools.tool(), Tools::Select);

    QTest::keyClick(&window, Qt::Key_D);
    QCOMPARE(tools.tool(), Tools::Draw);
    QCOMPARE(tools.toolName(), "draw");
    QVERIFY(!window.cursor().pixmap().isNull());

    QTest::keyClick(&window, Qt::Key_T);
    QCOMPARE(window.cursor().shape(), Qt::IBeamCursor);

    QTest::keyClick(&window, Qt::Key_A);
    QCOMPARE(window.cursor().shape(), Qt::CrossCursor);

    QTest::keyClick(&window, Qt::Key_Escape);
    QCOMPARE(tools.tool(), Tools::Select);

    // Ctrl and Super count as the same modifier; neither is a plain key.
    QTest::keyClick(&window, Qt::Key_D, Qt::ControlModifier);
    QCOMPARE(tools.tool(), Tools::Select);
    QTest::keyClick(&window, Qt::Key_D, Qt::MetaModifier);
    QCOMPARE(tools.tool(), Tools::Select);

    // The ink keys need no page (nothing reads them back; the page takes
    // the colour when one is set).
    QTest::keyClick(&window, Qt::Key_2);
}

QTEST_MAIN(SkeletonTest)
#include "tst_skeleton.moc"
