#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QtTest>

#include "quick/codeeditor.h"

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

class TstCodeEditor : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void rendersBackground() {
    QQuickView view;
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QVERIFY(editor);
    QVERIFY(editor->flags().testFlag(QQuickItem::ItemHasContents));
    const QImage image = view.grabWindow();
    QVERIFY(!image.isNull());
    QCOMPARE(image.pixelColor(10, 10), QColor(0x1e, 0x1e, 0x1e));
  }

  void setTextUpdatesLineCount() {
    CodeEditor editor;
    QSignalSpy spy(&editor, &CodeEditor::lineCountChanged);
    editor.setText(QStringLiteral("a\nb\nc"));
    QCOMPARE(editor.lineCount(), 3);
    QVERIFY(spy.count() >= 1);
  }
};

QTEST_MAIN(TstCodeEditor)
#include "tst_codeeditor.moc"
