// The real editor with a SyntaxHighlighter assigned from QML: highlighted text reaches the screen.
#include "core/highlighter.h"
#include "quick/codeeditor.h"
#include "quick/theme.h"
#include "syntax/languageregistry.h"
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
    highlighter: SyntaxHighlighter { objectName: "hl"; fileName: "a.js" }
})",
      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *editor = qobject_cast<CodeEditor *>(root.data());
    QVERIFY(editor);
    auto *highlighter = qobject_cast<qce::TreeSitterHighlighter *>(editor->highlighter());
    QVERIFY(highlighter);
    QCOMPARE(highlighter->detectedLanguage(), u"javascript"_s);

    QQuickView view(&m_engine, nullptr);
    editor->setParentItem(view.contentItem());
    view.resize(400, 120);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    editor->setText(u"function main() { return 0; }\n// done\n"_s);

    const QColor keyword(0x56, 0x9c, 0xd6), comment(0x6a, 0x99, 0x55);
    QTRY_VERIFY_WITH_TIMEOUT(countColor(view.grabWindow(), keyword) > 0, 10000);
    QVERIFY(countColor(view.grabWindow(), comment) > 0);

    // Switching the language away restyles the text.
    highlighter->setLanguage(u"plain"_s);
    QTRY_COMPARE(countColor(view.grabWindow(), keyword), 0);
  }

  // API-12/13: a host language, registered while an editor already exists, highlights in a host style.
  void aLanguageRegisteredLaterAppliesOnTheNextFileName() {
    QQmlComponent component(&m_engine);
    component.setData(
      R"(import QtQuick
import me.blq.qmlcodeeditor
import me.blq.qmlcodeeditor.syntax
CodeEditor {
    width: 400; height: 120
    highlighter: SyntaxHighlighter { fileName: "a.hostlang" }
})",
      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *editor = qobject_cast<CodeEditor *>(root.data());
    auto *highlighter = qobject_cast<qce::TreeSitterHighlighter *>(editor->highlighter());
    QVERIFY(highlighter);
    QCOMPARE(highlighter->detectedLanguage(), QString()); // not known yet

    const QColor hostColor(0xff, 0x00, 0xff);
    qce::registerTokenStyle(u"host.string");
    editor->theme()->setTokenStyle(u"host.string"_s, hostColor);
    qce::LanguageInfo info;
    info.id = u"hostlang"_s;
    info.name = u"Host language"_s;
    info.extensions = {u"hostlang"_s};
    info.aliases = {u"hl"_s};
    info.grammar = qce::LanguageRegistry::instance().find(u"json"_s)->grammar;
    info.highlightSource = u"(string) @host.string\n"_s;
    QString error;
    QVERIFY2(qce::LanguageRegistry::registerLanguage(info, &error), qPrintable(error));
    QCOMPARE(highlighter->detectedLanguage(), QString()); // an editor picks it up when its language is resolved again

    QQuickView view(&m_engine, nullptr);
    editor->setParentItem(view.contentItem());
    view.resize(400, 120);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    editor->setText(u"{\"key\": \"value\"}\n"_s);
    highlighter->setFileName(u"b.hostlang"_s);
    QCOMPARE(highlighter->detectedLanguage(), u"hostlang"_s);
    QTRY_VERIFY_WITH_TIMEOUT(countColor(view.grabWindow(), hostColor) > 0, 10000);

    // The language can be chosen by alias as well.
    highlighter->setFileName(QString());
    highlighter->setLanguage(u"HL"_s);
    QCOMPARE(highlighter->detectedLanguage(), u"hostlang"_s);
    QVERIFY(qce::TreeSitterHighlighter::availableLanguages().contains(u"hostlang"_s));
    QCOMPARE(qce::TreeSitterHighlighter::languageName(u"hostlang"_s), u"Host language"_s);
  }

  // A registered language is an injection target like the built-in ones: fenced code in Markdown.
  void aRegisteredLanguageIsInjectedIntoMarkdown() {
    const QColor hostColor(0xff, 0x00, 0xff);
    qce::registerTokenStyle(u"host.string");
    if (!qce::LanguageRegistry::instance().find(u"hostlang"_s)) {
      qce::LanguageInfo info;
      info.id = u"hostlang"_s;
      info.grammar = qce::LanguageRegistry::instance().find(u"json"_s)->grammar;
      info.aliases = {u"hl"_s};
      info.highlightSource = u"(string) @host.string\n"_s;
      QVERIFY(qce::LanguageRegistry::registerLanguage(info));
    }
    QQmlComponent component(&m_engine);
    component.setData(
      R"(import QtQuick
import me.blq.qmlcodeeditor
import me.blq.qmlcodeeditor.syntax
CodeEditor {
    width: 400; height: 160
    highlighter: SyntaxHighlighter { language: "markdown" }
})",
      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *editor = qobject_cast<CodeEditor *>(root.data());
    editor->theme()->setTokenStyle(u"host.string"_s, hostColor);
    QQuickView view(&m_engine, nullptr);
    editor->setParentItem(view.contentItem());
    view.resize(400, 160);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    editor->setText(u"text\n\n```hl\n{\"k\": \"v\"}\n```\n"_s);
    QTRY_VERIFY_WITH_TIMEOUT(countColor(view.grabWindow(), hostColor) > 0, 10000);
  }

  // The overlays list takes any highlighter from QML, so a SyntaxHighlighter can paint over another.
  void overlaysCanBeAssignedFromQml() {
    QQmlComponent component(&m_engine);
    component.setData(
      R"(import QtQuick
import me.blq.qmlcodeeditor
import me.blq.qmlcodeeditor.syntax
CodeEditor {
    width: 400; height: 120
    highlighter: SyntaxHighlighter { language: "javascript" }
    overlays: [ SyntaxHighlighter { objectName: "over"; language: "json" } ]
})",
      QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *editor = qobject_cast<CodeEditor *>(root.data());
    QCOMPARE(editor->overlayList().size(), 1);
    QCOMPARE(editor->overlayList().first()->objectName(), u"over"_s);
    auto *over = qobject_cast<qce::TreeSitterHighlighter *>(editor->overlayList().first());
    QVERIFY(over);
    QCOMPARE(over->detectedLanguage(), u"json"_s);
  }
};

QTEST_MAIN(TstEditorSyntax)
#include "tst_editorsyntax.moc"
