#include <QtQuick/QQuickWindow>
#include <QtTest>

#include "tools.h"

class SkeletonTest : public QObject
{
    Q_OBJECT

private slots:
    void keyPressChangesTool();
};

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

    QTest::keyClick(&window, Qt::Key_2);
    QCOMPARE(tools.ink(), Tools::Red);
}

QTEST_MAIN(SkeletonTest)
#include "tst_skeleton.moc"
