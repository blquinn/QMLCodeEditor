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

  void themeSwitchRepaints() {
    QQuickView view;
    view.setSource(QUrl::fromLocalFile(QFINDTESTDATA("editor.qml")));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *editor = qobject_cast<CodeEditor *>(view.rootObject());
    QVERIFY(editor && editor->theme());
    qce::Theme light;
    light.assign(*std::unique_ptr<qce::Theme>(qce::Theme::createLight()));
    editor->setTheme(&light);
    QCOMPARE(editor->theme(), &light);
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 10), QColor(0xff, 0xff, 0xff));
    light.setProperty("background", QColor(0x10, 0x20, 0x30));
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 10), QColor(0x10, 0x20, 0x30));
    editor->setTheme(nullptr); // back to the owned default
    QTRY_COMPARE(view.grabWindow().pixelColor(10, 10), QColor(0x1e, 0x1e, 0x1e));
  }

  void themeMapsSpansToFormats() {
    std::unique_ptr<qce::Theme> theme(qce::Theme::createDark());
    using qce::HighlightSpan;
    using qce::TokenStyle;
    const auto ranges = theme->formatRanges(
      {{0, 3, TokenStyle::Keyword}, {4, 2, TokenStyle::Default}, {7, 5, TokenStyle::Comment}}
    );
    QCOMPARE(ranges.size(), 2);
    QCOMPARE(ranges[0].start, 0);
    QCOMPARE(ranges[0].length, 3);
    QCOMPARE(ranges[0].format.foreground().color(), QColor(QStringLiteral("#569cd6")));
    QVERIFY(ranges[1].format.fontItalic());
    QCOMPARE(theme->charFormat(TokenStyle::Default).foreground().color(), theme->foreground());
    // Changing the foreground reaches styles the theme doesn't list.
    theme->setProperty("foreground", QColor(Qt::red));
    QCOMPARE(theme->charFormat(TokenStyle::Default).foreground().color(), QColor(Qt::red));
  }

  void highlighterInvalidationRepaints() {
    struct Fake : qce::Highlighter {
      QList<QList<qce::HighlightSpan>>
      highlightLines(const qce::TextSnapshot &, qsizetype, qsizetype) override {
        return {};
      }
    } fake;
    CodeEditor editor;
    QVERIFY(editor.highlighter());
    editor.setHighlighter(&fake);
    QCOMPARE(editor.highlighter(), &fake);
    emit fake.invalidated(0, 3); // must not crash with an item that has no window
    editor.setHighlighter(nullptr);
    QVERIFY(editor.highlighter() != &fake);
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
