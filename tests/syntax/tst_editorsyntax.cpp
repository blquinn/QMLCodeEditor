// The real editor with a SyntaxHighlighter assigned from QML: highlighted text reaches the screen.
#include "quick/codeeditor.h"
#include "syntax/treesitterhighlighter.h"

#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/qsgrendererinterface.h>
#include <QtTest>

using namespace Qt::StringLiterals;

Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditorPlugin)
Q_IMPORT_QML_PLUGIN(me_blq_qmlcodeeditor_syntaxPlugin)

class TstEditorSyntax : public QObject {
  Q_OBJECT
  QQmlEngine m_engine;

  // Pixels close to `color` (antialiasing blends small text with the background).
  static int countColor(const QImage &image, const QColor &color) {
    int n = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const QColor c = image.pixelColor(x, y);
        if (qAbs(c.red() - color.red()) < 24 && qAbs(c.green() - color.green()) < 24 &&
            qAbs(c.blue() - color.blue()) < 24)
          ++n;
      }
    }
    return n;
  }

private slots:
  void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }

  void highlightedTextIsDrawn() {
    QQmlComponent component(&m_engine);
    component.setData(
      R"(import QtQuick
import me.blq.qmlcodeeditor
import me.blq.qmlcodeeditor.syntax
CodeEditor {
    width: 400; height: 120
    highlighter: SyntaxHighlighter { objectName: "hl"; fileName: "a.cpp" }
})",
      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *editor = qobject_cast<CodeEditor *>(root.data());
    QVERIFY(editor);
    auto *highlighter = qobject_cast<qce::TreeSitterHighlighter *>(editor->highlighter());
    QVERIFY(highlighter);
    QCOMPARE(highlighter->detectedLanguage(), u"cpp"_s);

    QQuickView view(&m_engine, nullptr);
    editor->setParentItem(view.contentItem());
    view.resize(400, 120);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    editor->setText(u"int main() { return 0; }\n// done\n"_s);

    const QColor keyword(0x56, 0x9c, 0xd6), comment(0x6a, 0x99, 0x55);
    QTRY_VERIFY_WITH_TIMEOUT(countColor(view.grabWindow(), keyword) > 0, 10000);
    QVERIFY(countColor(view.grabWindow(), comment) > 0);

    // Switching the language away restyles the text.
    highlighter->setLanguage(u"plain"_s);
    QTRY_COMPARE(countColor(view.grabWindow(), keyword), 0);
  }
};

QTEST_MAIN(TstEditorSyntax)
#include "tst_editorsyntax.moc"
