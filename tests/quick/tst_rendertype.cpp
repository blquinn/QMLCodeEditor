#include <QtGui/QGuiApplication>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtTest>

#include "quick/codeeditor.h"

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)

class TstRenderType : public QObject {
  Q_OBJECT
  QQmlEngine m_engine;
  QQuickWindow::TextRenderType m_savedDefault = QQuickWindow::textRenderType();

  struct Shown {
    std::unique_ptr<QQuickView> view;
    CodeEditor *editor = nullptr;
  };
  Shown showEditor() {
    Shown shown;
    shown.view = std::make_unique<QQuickView>(&m_engine, nullptr);
    shown.view->setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    shown.editor = qobject_cast<CodeEditor *>(shown.view->rootObject());
    shown.editor->setWidth(400);
    shown.editor->setHeight(200);
    shown.editor->setText(QStringLiteral("one\ntwo\nthree\nfour\n"));
    shown.view->show();
    if (!QTest::qWaitForWindowExposed(shown.view.get()))
      return {};
    shown.editor->setCursorBlinkInterval(0);
    return shown;
  }
  // What the scene's text nodes were last set to; QTRY_COMPARE re-evaluates it until a frame lands.
  static QSGTextNode::RenderType nodeType(CodeEditor *editor) {
    QTest::qWait(0);
    return editor->renderStats().scene.renderType;
  }
  static bool drawn(CodeEditor *editor) { return editor->renderStats().scene.nodesCreated > 0; }

private slots:
  void init() { QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering); }
  void cleanupTestCase() { QQuickWindow::setTextRenderType(m_savedDefault); }

  void mapping() {
    QCOMPARE(CodeEditor::toNodeRenderType(CodeEditor::QtRendering), QSGTextNode::QtRendering);
    QCOMPARE(CodeEditor::toNodeRenderType(CodeEditor::NativeRendering), QSGTextNode::NativeRendering);
    QCOMPARE(CodeEditor::toNodeRenderType(CodeEditor::CurveRendering), QSGTextNode::CurveRendering);
    QCOMPARE(CodeEditor::fromWindowRenderType(QQuickWindow::QtTextRendering), CodeEditor::QtRendering);
    QCOMPARE(CodeEditor::fromWindowRenderType(QQuickWindow::NativeTextRendering), CodeEditor::NativeRendering);
    QCOMPARE(CodeEditor::fromWindowRenderType(QQuickWindow::CurveTextRendering), CodeEditor::CurveRendering);
    // Same values as QSGTextNode::RenderType, which Text.renderType mirrors.
    QCOMPARE(int(CodeEditor::QtRendering), int(QSGTextNode::QtRendering));
    QCOMPARE(int(CodeEditor::NativeRendering), int(QSGTextNode::NativeRendering));
    QCOMPARE(int(CodeEditor::CurveRendering), int(QSGTextNode::CurveRendering));
  }

  void followsWindowDefault() {
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    auto shown = showEditor();
    QVERIFY(shown.editor);
    QCOMPARE(shown.editor->renderType(), CodeEditor::NativeRendering);
    QTRY_VERIFY(drawn(shown.editor));
    QCOMPARE(nodeType(shown.editor), QSGTextNode::NativeRendering);

    // The default is read per frame, not cached at construction.
    QQuickWindow::setTextRenderType(QQuickWindow::CurveTextRendering);
    QCOMPARE(shown.editor->renderType(), CodeEditor::CurveRendering);
    shown.editor->update();
    QTRY_COMPARE(nodeType(shown.editor), QSGTextNode::CurveRendering);
  }

  void readBeforeWindow() {
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    CodeEditor editor;
    QCOMPARE(editor.renderType(), CodeEditor::NativeRendering);
  }

  void explicitOverrideWins() {
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    auto shown = showEditor();
    QVERIFY(shown.editor);
    QSignalSpy spy(shown.editor, &CodeEditor::renderTypeChanged);
    shown.editor->setRenderType(CodeEditor::CurveRendering);
    QCOMPARE(shown.editor->renderType(), CodeEditor::CurveRendering);
    QCOMPARE(spy.count(), 1);
    shown.editor->setRenderType(CodeEditor::CurveRendering);
    QCOMPARE(spy.count(), 1);
    QTRY_COMPARE(nodeType(shown.editor), QSGTextNode::CurveRendering);

    // The window default no longer matters.
    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);
    shown.editor->update();
    QTest::qWait(50);
    QCOMPARE(nodeType(shown.editor), QSGTextNode::CurveRendering);
  }

  void changeUpdatesExistingNodes() {
    auto shown = showEditor();
    QVERIFY(shown.editor);
    QTRY_VERIFY(drawn(shown.editor));
    QCOMPARE(nodeType(shown.editor), QSGTextNode::QtRendering);
    const quint64 filled = shown.editor->renderStats().scene.linesFilled;
    const quint64 created = shown.editor->renderStats().scene.nodesCreated;

    shown.editor->setRenderType(CodeEditor::NativeRendering);
    QTRY_COMPARE(nodeType(shown.editor), QSGTextNode::NativeRendering);
    // Existing nodes were refilled, not replaced.
    QVERIFY(shown.editor->renderStats().scene.linesFilled > filled);
    QCOMPARE(shown.editor->renderStats().scene.nodesCreated, created);

    // Steady state: no further refills.
    const quint64 settled = shown.editor->renderStats().scene.linesFilled;
    shown.editor->update();
    QTest::qWait(50);
    QCOMPARE(shown.editor->renderStats().scene.linesFilled, settled);
  }

  void metricsIndependent() {
    auto shown = showEditor();
    QVERIFY(shown.editor);
    const qreal lineHeight = shown.editor->metrics().lineHeight();
    const qreal advance = shown.editor->metrics().cellAdvance();
    const qreal contentWidth = shown.editor->contentWidth();
    shown.editor->setRenderType(CodeEditor::NativeRendering);
    QTRY_COMPARE(nodeType(shown.editor), QSGTextNode::NativeRendering);
    QCOMPARE(shown.editor->metrics().lineHeight(), lineHeight);
    QCOMPARE(shown.editor->metrics().cellAdvance(), advance);
    QCOMPARE(shown.editor->contentWidth(), contentWidth);
  }

  void resetRestoresDefault() {
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    auto shown = showEditor();
    QVERIFY(shown.editor);
    shown.editor->setRenderType(CodeEditor::CurveRendering);
    QTRY_COMPARE(nodeType(shown.editor), QSGTextNode::CurveRendering);

    QSignalSpy spy(shown.editor, &CodeEditor::renderTypeChanged);
    shown.editor->resetRenderType();
    QCOMPARE(shown.editor->renderType(), CodeEditor::NativeRendering);
    QCOMPARE(spy.count(), 1);
    QTRY_COMPARE(nodeType(shown.editor), QSGTextNode::NativeRendering);

    // Follows the window default again.
    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);
    shown.editor->update();
    QTRY_COMPARE(nodeType(shown.editor), QSGTextNode::QtRendering);
  }

  void qmlProperty() {
    auto shown = showEditor();
    QVERIFY(shown.editor);
    QVERIFY(shown.editor->setProperty("renderType", CodeEditor::NativeRendering));
    QCOMPARE(shown.editor->property("renderType").toInt(), int(CodeEditor::NativeRendering));
  }
};

QTEST_MAIN(TstRenderType)
#include "tst_rendertype.moc"
